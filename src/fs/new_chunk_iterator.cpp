
new_chunk_iterator_t::new_chunk_iterator_t(scan_task_ptr_t task_, file_ptr_t backend_) noexcept
    : task{std::move(task)}, backend{std::move(backend_)}, abandoned{false}, invalid{false} {}
