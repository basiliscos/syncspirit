// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once
#include "base.h"
#include <spdlog/spdlog.h>
#include <boost/asio/ssl.hpp>
#include "stream.h"

namespace syncspirit::transport {

namespace ssl = asio::ssl;
using tcp_socket_t = tcp::socket;
using ssl_socket_t = ssl::stream<tcp::socket>;

template <typename T> struct error_curry_t : model::arc_base_t<error_curry_t<T>> {
    using backend_ptr_t = model::intrusive_ptr_t<T>;
    error_curry_t(T &owner, error_fn_t &on_error) noexcept : backend{&owner}, on_error_fn{std::move(on_error)} {}
    virtual ~error_curry_t(){};

    void error(const sys::error_code &ec) noexcept {
        on_error_fn(ec);
        backend->supervisor.do_process();
    }

    backend_ptr_t backend;
    error_fn_t on_error_fn;
};

template <typename T, typename F> struct success_curry_t : error_curry_t<T> {
    using parent_t = error_curry_t<T>;
    using backend_ptr_t = model::intrusive_ptr_t<T>;

    success_curry_t(T &owner, F &success_fn_, error_fn_t &on_error) noexcept
        : parent_t(owner, on_error), success_fn{std::move(success_fn_)} {}

    template <typename... Args> void success(Args &&...args) noexcept {
        success_fn(std::forward<Args>(args)...);
        parent_t::backend->supervisor.do_process();
    }

    F success_fn;
};

template <typename T, typename... Args> auto curry_connect(Args &&...args) {
    using sp_t = model::intrusive_ptr_t<success_curry_t<T, connect_fn_t>>;
    return sp_t(new typename sp_t::element_type(std::forward<Args>(args)...));
}

template <typename T, typename... Args> auto curry_handshake(Args &&...args) {
    using sp_t = model::intrusive_ptr_t<success_curry_t<T, handshake_fn_t>>;
    return sp_t(new typename sp_t::element_type(std::forward<Args>(args)...));
}

template <typename T, typename... Args> auto curry_io(Args &&...args) {
    using sp_t = model::intrusive_ptr_t<success_curry_t<T, io_fn_t>>;
    return sp_t(new typename sp_t::element_type(std::forward<Args>(args)...));
}

// --------------------------------

template <typename Sock> struct base_impl_t;

template <> struct base_impl_t<tcp_socket_t> {
    rotor::asio::supervisor_asio_t &supervisor;
    strand_t &strand;
    tcp_socket_t sock;
    bool cancelling = false;
    base_impl_t(transport_config_t &config) noexcept
        : supervisor{config.supervisor}, strand{supervisor.get_strand()}, sock(strand.context()) {}

    tcp_socket_t &get_physical_layer() noexcept { return sock; }
};

template <> struct base_impl_t<ssl_socket_t> {
    using self_t = base_impl_t<ssl_socket_t>;

    rotor::asio::supervisor_asio_t &supervisor;
    strand_t &strand;
    model::device_id_t expected_peer;
    model::device_id_t actual_peer;
    const utils::key_pair_t *me = nullptr;
    ssl::context ctx;
    ssl::stream_base::handshake_type role;
    ssl_socket_t sock;
    bool validation_passed = false;
    bool cancelling = false;

    static ssl::context get_context(self_t &source, std::string_view alpn) noexcept {
        ssl::context ctx(ssl::context::tls);
        ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);

        auto me = source.me;
        if (me) {
            auto &cert_data = me->cert_data.bytes;
            auto &key_data = me->key_data.bytes;
            ctx.use_certificate(asio::const_buffer(cert_data.c_str(), cert_data.size()), ssl::context::asn1);
            ctx.use_private_key(asio::const_buffer(key_data.c_str(), key_data.size()), ssl::context::asn1);
        }

        if (alpn.size()) {
            std::byte wire_alpn[alpn.size() + 1];
            wire_alpn[0] = (std::byte)(alpn.size());
            auto b = reinterpret_cast<const std::byte *>(alpn.data());
            std::copy(b, b + alpn.size(), wire_alpn + 1);
            auto r = SSL_CTX_set_alpn_protos(ctx.native_handle(), (const unsigned char *)wire_alpn, alpn.size() + 1);
            assert(r == 0 && "SSL_CTX_set_alpn_protos");
            (void)r;
        }

        return ctx;
    }

    static ssl_socket_t mk_sock(transport_config_t &config, ssl::context &ctx, strand_t &strand) noexcept {
        if (config.sock) {
            tcp::socket sock(std::move(config.sock.value()));
            return {std::move(sock), ctx};
        } else {
            tcp::socket sock(strand.context());
            return {std::move(sock), ctx};
        }
    }

    base_impl_t(transport_config_t &config) noexcept
        : supervisor{config.supervisor}, strand{supervisor.get_strand()}, expected_peer{config.ssl_junction->peer},
          me(config.ssl_junction->me), ctx(get_context(*this, config.ssl_junction->alpn)),
          role(config.sock ? ssl::stream_base::server : ssl::stream_base::client), sock(mk_sock(config, ctx, strand)) {
        if (config.ssl_junction->sni_extension) {
            auto &host = config.uri.host;
            if (!SSL_set_tlsext_host_name(sock.native_handle(), host.c_str())) {
                sys::error_code ec{static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category()};
                spdlog::error("http_actor_t:: Set SNI Hostname : {}", ec.message());
            }
        }
        if (me) {
            auto mode = ssl::verify_peer | ssl::verify_fail_if_no_peer_cert | ssl::verify_client_once;
            sock.set_verify_mode(mode);
            sock.set_verify_depth(1);
            sock.set_verify_callback([&](bool, ssl::verify_context &peer_ctx) -> bool {
                auto native = peer_ctx.native_handle();
                auto peer_cert = X509_STORE_CTX_get_current_cert(native);
                if (!peer_cert) {
                    spdlog::warn("no peer certificate");
                    return false;
                }
                auto der_option = utils::as_serialized_der(peer_cert);
                if (!der_option) {
                    spdlog::warn("peer certificate cannot be serialized as der : {}", der_option.error().message());
                    return false;
                }

                utils::cert_data_t cert_data{std::move(der_option.value())};
                auto peer_option = model::device_id_t::from_cert(cert_data);
                if (!peer_option) {
                    spdlog::warn("cannot get device_id from peer");
                    return false;
                }

                auto peer = std::move(peer_option.value());
                if (!actual_peer) {
                    actual_peer = std::move(peer);
                    spdlog::trace("tls, peer device_id = {}", actual_peer);
                }

                if (role == ssl::stream_base::handshake_type::client) {
                    if (actual_peer != expected_peer) {
                        spdlog::warn("unexcpected peer device_id. Got: {}, expected: {}", actual_peer, expected_peer);
                        return false;
                    }
                }
                validation_passed = true;
                return true;
            });
        }
    }

    tcp_socket_t &get_physical_layer() noexcept { return sock.next_layer(); }
};

// --------------------------------

template <typename Sock, typename Owner> inline void generic_async_send(Owner owner, asio::const_buffer buff) noexcept {
    auto &sock = owner->backend->sock;
    asio::async_write(sock, buff, [owner = std::move(owner)](auto ec, auto bytes) mutable {
        auto &strand = owner->backend->strand;
        if (ec) {
            strand.post([ec = ec, owner = std::move(owner)]() mutable {
                if (ec == asio::error::operation_aborted) {
                    owner->backend->cancelling = false;
                }
                owner->error(ec);
            });
            return;
        }
        strand.post([owner = std::move(owner), bytes]() mutable { owner->success(bytes); });
    });
}

template <typename Sock, typename Owner>
inline void generic_async_recv(Owner owner, asio::mutable_buffer buff) noexcept {
    auto &sock = owner->backend->sock;
    sock.async_read_some(buff, [owner = std::move(owner)](auto ec, auto bytes) mutable {
        auto &strand = owner->backend->strand;
        if (ec) {
            strand.post([ec = ec, owner = std::move(owner)]() mutable {
                if (ec == asio::error::operation_aborted) {
                    owner->backend->cancelling = false;
                }
                owner->error(ec);
            });
            return;
        }
        strand.post([owner = std::move(owner), bytes]() mutable { owner->success(bytes); });
    });
}

template <typename T> struct impl;

template <> struct impl<tcp_socket_t> {
    using socket_t = tcp_socket_t;

    template <typename Owner> inline static void async_connect(Owner owner, const resolved_hosts_t &hosts) noexcept {
        auto &sock = owner->backend->get_physical_layer();
        asio::async_connect(sock, hosts.begin(), hosts.end(), [owner](auto ec, auto addr) mutable {
            auto &strand = owner->backend->strand;
            if (ec) {
                strand.post([ec = ec, owner = std::move(owner)]() mutable {
                    if (ec == asio::error::operation_aborted) {
                        owner->backend->cancelling = false;
                    }
                    owner->error(ec);
                });
                return;
            }
            strand.post([addr = addr, owner = std::move(owner)]() mutable { owner->success(addr); });
        });
    }

    template <typename Owner> inline static void async_handshake(Owner owner) noexcept {
        sys::error_code ec;
        auto endpoint = owner->backend->sock.remote_endpoint(ec);
        auto &strand = owner->backend->strand;
        if (ec) {
            strand.post([ec = ec, owner = std::move(owner)]() mutable { owner->error(ec); });
            return;
        } else {
            strand.post([endpoint, owner = std::move(owner)]() mutable {
                utils::x509_t x509;
                owner->success(true, x509, endpoint, nullptr);
            });
        }
    }

    template <typename Owner> inline static void async_send(Owner owner, asio::const_buffer buff) noexcept {
        generic_async_send<socket_t, Owner>(std::move(owner), buff);
    }

    template <typename Owner> inline static void async_recv(Owner owner, asio::mutable_buffer buff) noexcept {
        generic_async_recv<socket_t, Owner>(std::move(owner), buff);
    }

    template <typename Backend> inline static void cancel(Backend &backend, socket_t &sock) noexcept {
        if (!backend.cancelling) {
            backend.cancelling = true;
            sys::error_code ec;
            sock.cancel(ec);
            if (ec) {
                spdlog::error("impl<tcp::socket>::cancel() :: {}", ec.message());
            }
        }
    }
};

template <> struct impl<ssl_socket_t> {
    using socket_t = ssl_socket_t;

    template <typename Owner> inline static void async_connect(Owner owner, const resolved_hosts_t &hosts) noexcept {
        impl<tcp_socket_t>::async_connect<Owner>(std::move(owner), hosts);
    }

    template <typename Owner> inline static void async_handshake(Owner owner) noexcept {
        auto &role = owner->backend->role;
        auto &sock = owner->backend->sock;
        sock.async_handshake(role, [owner = std::move(owner)](auto ec) {
            auto &strand = owner->backend->strand;
            if (ec) {
                strand.post([ec = ec, owner = std::move(owner)]() { owner->error(ec); });
                return;
            }
            auto endpoint = owner->backend->get_physical_layer().remote_endpoint(ec);
            if (ec) {
                strand.post([ec, owner = std::move(owner)]() {
                    if (ec == asio::error::operation_aborted) {
                        owner->backend->cancelling = false;
                    }
                    owner->error(ec);
                });
                return;
            }
            strand.post([endpoint, owner = std::move(owner)]() {
                auto &backend = owner->backend;
                auto &sock = backend->sock;
                auto peer_cert = SSL_get_peer_certificate(sock.native_handle());
                auto x509 = utils::x509_t(peer_cert);
                owner->success(true, x509, endpoint, &backend->actual_peer);
            });
        });
    }

    template <typename Owner> inline static void async_send(Owner owner, asio::const_buffer buff) noexcept {
        generic_async_send<socket_t, Owner>(std::move(owner), buff);
    }

    template <typename Owner> inline static void async_recv(Owner owner, asio::mutable_buffer buff) noexcept {
        generic_async_recv<socket_t, Owner>(std::move(owner), buff);
    }

    template <typename Backend> inline static void cancel(Backend &backend, socket_t &sock) noexcept {
        impl<tcp_socket_t>::cancel(backend, sock.next_layer());
    }
};

// -------------------------------------

template <typename T, typename Sock, typename P> struct interface_t : P {
    using self_t = T;

    inline self_t &get_self() noexcept { return static_cast<self_t &>(*this); }

    void async_connect(const resolved_hosts_t &hosts, connect_fn_t &on_connect,
                       error_fn_t &on_error) noexcept override {
        auto curry = curry_connect<self_t>(get_self(), on_connect, on_error);
        impl<Sock>::async_connect(std::move(curry), hosts);
    }

    void async_handshake(handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept override {
        auto curry = curry_handshake<self_t>(get_self(), on_handshake, on_error);
        impl<Sock>::async_handshake(std::move(curry));
    }

    void async_send(asio::const_buffer buff, io_fn_t &on_write, error_fn_t &on_error) noexcept override {
        auto curry = curry_io<self_t>(get_self(), on_write, on_error);
        impl<Sock>::async_send(std::move(curry), buff);
    }

    void async_recv(asio::mutable_buffer buff, io_fn_t &on_read, error_fn_t &on_error) noexcept override {
        auto curry = curry_io<self_t>(get_self(), on_read, on_error);
        impl<Sock>::async_recv(std::move(curry), buff);
    }

    void cancel() noexcept override { impl<Sock>::cancel(get_self(), get_self().sock); }

    asio::ip::address local_address(sys::error_code &ec) noexcept override {
        auto &sock = get_self().get_physical_layer();
        auto endpoint = sock.local_endpoint(ec);
        if (!ec) {
            return endpoint.address();
        }
        return {};
    }

    ~interface_t() { get_self().get_physical_layer().close(); }
};

} // namespace syncspirit::transport
