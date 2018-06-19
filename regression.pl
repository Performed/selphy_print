#!/usr/bin/perl
use strict;

my $copies = 3;
my $retval = 0;

while (<STDIN>) {
    chomp;
    next if /^#/;
    my @row = split(/,/);

    $ENV{"TEST_MODE"} = "2";
    $ENV{"BACKEND"} = $row[0];
    $ENV{"EXTRA_VID"} = $row[1];
    $ENV{"EXTRA_PID"} = $row[2];

    print "***** $row[0] $row[1] $row[2] $row[3]\n";

    foreach (my $i = 1; $i <= $copies ; $i++) {
	my @args = ("valgrind", "./dyesub_backend", "-d", $i, "testjobs/${row[3]}");

	my $rval = system(@args);

	if ($rval < 0) {
	    print "***** FAIL: failure to launch ($rval)\n";
	    $retval++;
	} elsif ($rval > 0) {
	    print "***** FAIL: failure to parse/execute ($rval) $row[0] $row[1] $row[2] $row[3] \n";
	    $retval++;
	} else {
	    print "***** PASS\n";
	}
    }
}
exit($retval);
