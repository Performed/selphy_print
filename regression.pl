#!/usr/bin/perl
use strict;

my @array;

while (<STDIN>) {
    chomp;
    next if /^#/;
    my @row = split(/,/);

    $ENV{"TEST_MODE"} = "2";
    $ENV{"BACKEND"} = $row[0];
    $ENV{"EXTRA_VID"} = $row[1];
    $ENV{"EXTRA_PID"} = $row[2];
    $ENV{"EXTRA_TYPE"} = $row[3];

    print "***** $row[0] $row[1] $row[2] $row[3] $row[4]\n";
    my @args = ("valgrind", "./dyesub_backend", "testjobs/${row[4]}");

    my $rval = system(@args);

    if ($rval < 0) {
	print "***** FAIL: failure to launch ($rval)\n";	
    } elsif ($rval > 0) {
	print "***** FAIL: failure to parse/execute ($rval)\n";
    } else {
	print "***** PASS\n";
    }
}
    
