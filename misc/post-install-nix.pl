#!/usr/bin/env perl
use strict;
use warnings;
use 5.30.0;

use File::Copy;
use File::Basename;

my ($original_bin, $tuned_bin) = ($ARGV[0], $ARGV[1]);
die("usage: $0 path/to/original/executable path/to/modified/executable")
    if (!$original_bin || !$tuned_bin);

# returns hash of ALL (recursively) resolved dependecies,
# i.e. {lib_1.so => /path/to/lib_1.so, ... }
my $resolve = sub {
    my $binary = shift;
    my $list = `ldd $binary 2>&1`;
    my %mapping =
        map  { m/(.*) => (.*)/; ($1, $2) }
        map  { s/\s*(lib.*) \(.*/$1/r }
        grep { /\s*lib.* => / }
        split /\n/, $list;
    return \%mapping;
};

# returns true if library should be skipped (i.e. it is a SYSTEM dependency)
my $skip = sub {
    my $path = shift;
    return scalar($path =~ m{^(/usr/|/lib/)});
};

my $dest_dir = dirname($tuned_bin);
my $copied = 0;
my $resolved = $resolve->($original_bin);

for my $lib  (keys $resolved->%*) {
    my $path = $resolved->{$lib};
    next if $skip->($path);
    my $target = "$dest_dir/$lib";
    next if (-e $target);
    if (File::Copy::syscopy($path, $target)) {
        say "[copied] $lib";
        ++$copied;
    } else {
        say "[error] cannot copy $lib ($path -> $target): $!";
    }
    say $lib;
}

say "totally copied $copied libs";

=x
my $info = $read_elf->($tuned_bin);
my $all_libs = $get_dynamic->($info);
my $dest_dir = $get_rpath->($info);
my $resolved = $resolve->($original_bin);
my $processed = {};
my $copied = 0;

say "dest_lib_dir = $dest_dir";
mkdir($dest_dir);

# try to copy each found dependecy
while (my $lib = shift($all_libs->@*)) {
    # skip already processed and not resolved dependency
    next if exists $processed->{$lib};
    say "zzz $lib";
    #if (exists $resolved->{$lib}) {
    #    my $r = $resolved->{$lib};
    #    next unless $r =~ m|/home/|;
    #}

    my $source_lib = $resolved->{$lib};
    my $dest_lib = "$dest_dir/$lib";

    # skip already existing (copied) dependency
    if (-e $dest_lib) {
        $processed->{$lib} = 1;
        next;
    }
    if ($skip->($source_lib)) {
        #say "[skip] $lib";
        next;
    }

    if (File::Copy::syscopy($source_lib, $dest_lib)) {
        say "[copy] $lib ($source_lib)";
        # $DB::single = 1 if ($lib eq 'libwx_gtk3u_richtext-3.2.so.0');
        ++$copied;
        # gather dependencies of the depndency
        my $lib_info = $read_elf->($source_lib);
        my $lib_libs = $get_dynamic->($lib_info);
        my $libs_resolved = $resolve->($source_lib);
        # append gathered dependencies if needed
        for my $l (@$lib_libs) {
            # $DB::single = 1 if ($l eq 'libwx_baseu_xml-3.2.so.0');
            next if exists $processed->{$l};
            next if not exists $libs_resolved->{$l};
            my $dep_path = $libs_resolved->{$l};
            next if $skip->($dep_path)  =~ m{/usr/|/lib/};
            $resolved->{$l} = $dep_path;
            push $all_libs->@*, $l;
        }

    } else {
        say "[error] $lib (from $source_lib): $!";
    }
    $processed->{$lib} = 1;
}

say "totally copied $copied libs";
=cut
