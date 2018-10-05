#!/usr/bin/perl
use strict;
use IPC::Run qw( run );

my $retval = 0;

my $id = 123;
my $user = "tester";
my $title = "image_test";
my $max_copies = 3;
my $input_image = "testjobs/s3s-59.png";
my $max_pages = 3;
my $valgrind = 0;

$ENV{"STP_SUPPRESS_VERBOSE_MESSAGES"} = 1;
$ENV{"OMP_NUM_THREADS"} = 1;
$ENV{"TEST_MODE"} = "2";

while (<STDIN>) {
    chomp;
    next if /^#/;
    s/(.+)#.*/$1/;

    if (defined($ARGV[0])) {
	next if (index($_,$ARGV[0]) == -1);
    };

    my @row = split(/,/);

    my $gp_name = $row[0];
    $ENV{"BACKEND"} = $row[0];
    $ENV{"EXTRA_VID"} = $row[2];
    $ENV{"EXTRA_PID"} = $row[3];

    if (length($row[4])) {
	$ENV{"MEDIA_CODE"} = $row[4];
    } else {
	undef($ENV{"MEDIA_CODE"});
    }

    my $options = "";

    my @gp_options = split(/;/,$row[5]);
    # generate options list
    foreach my $x (@gp_options) {
	$options .= "$x ";
    }

    print "***** $row[0] $row[1] $row[2] $row[3] $row[4] '$row[5]'\n";

    my @args;

    # Generate PPD
    my $ppd_fname = "/tmp/stp-$gp_name.5.3.ppd";

    $ENV{"PPD"} = $ppd_fname;
    $ENV{"DEVICE_URI"} = "gutenprint53+usb://$row[0]/12345678";

    run ["/usr/sbin/cups-genppd.5.3", "-p", "/tmp", "-Z", $gp_name] or die("FAIL genppd $?: $row[0] $row[1] $row[2] $row[3] $row[4] $row[5]\n");

    for (my $pages = 1 ; $pages <= $max_pages ; $pages++) {
	# generate PDF.
	@args = ("/usr/bin/convert");
	for (my $i = 0 ; $i < $pages ; $i++) {
	    push(@args, $input_image);
	}
	push(@args, "-density");
	push(@args, "300x300");
	push(@args, "/tmp/${gp_name}.pdf");
	print join(":", @args) . "\n";
	run \@args or die ("FAIL: convert: $?");

	# Generate raster from PDF
	@args = ("/usr/lib/cups/filter/pdftoraster", $id, $user, $title, 1, $options, "/tmp/${gp_name}.pdf");
	print join(":", @args) . "\n";
	run \@args, ">", "/tmp/${gp_name}.raster" or die ("FAIL: imagetoraster $?: $row[0] $row[1] $row[2] $row[3] $row[4] $row[5] $pages\n");

	for (my $copies = 1 ; $copies <= $max_copies ; $copies++) {
	    # Call raster2gutenprint
	    @args = ("/usr/lib/cups/filter/rastertogutenprint.5.3", $id, $user, $title, $copies, $options);
	    if ($valgrind) {
		unshift(@args,"valgrind");
	    }
	    print join(":", @args) . "\n";
	    run \@args, "<", "/tmp/${gp_name}.raster", ">", "/tmp/${gp_name}.raw" or die("FAIL: rastertogutenorint $?: $row[0] $row[1] $row[2] $row[3] $row[4] $row[5] $pages $copies\n");

	    # Call backend using CUPS methodologies, using STDIN.
	    @args = ("./dyesub_backend", $id, $user, $title, $copies, $options);
	    if ($valgrind) {
		unshift(@args,"valgrind");
	    }
	    print join(":", @args) . "\n";
	    run \@args, "<", "/tmp/${gp_name}.raw" or die("FAIL: backend $?: $row[0] $row[1] $row[2] $row[3] $row[4] $row[5] $pages $copies\n");
	}
    }

    unlink ("/tmp/${gp_name}.pdf");
    unlink ("/tmp/${gp_name}.raster");
    unlink ("/tmp/${gp_name}.raw");
    unlink ($ppd_fname);

    print "***** PASS\n";
}
exit($retval);
