#!/usr/bin/perl
use strict;
use IPC::Run qw( run );

my $retval = 0;

my $id = 123;
my $user = "tester";
my $title = "image_test";
my $max_copies = 3;
my $input_image = "testjobs/s3s-59.png";

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

	# Generate raster from $image
	@args = ("/usr/lib/cups/filter/imagetoraster", $id, $user, $title, 1, $options, $input_image);
	print join(":", @args) . "\n";
	run \@args, ">", "/tmp/${gp_name}.raster" or die ("FAIL: imagetoraster $
?: $row[0] $row[1] $row[2] $row[3] $row[4] $row[5]\n");

    for (my $copies = 1 ; $copies <= $max_copies ; $copies++) {
	# Call raster2gutenprint
	@args = ("valgrind", "/usr/lib/cups/filter/rastertogutenprint.5.3", $id, $user, $title, $copies, $options);
	print join(":", @args) . "\n";
	run \@args, "<", "/tmp/${gp_name}.raster", ">", "/tmp/${gp_name}.raw" or die("FAIL: rastertogutenorint $?: $row[0] $row[1] $row[2] $row[3] $row[4] $row[5]\n");

	# Call backend using CUPS methodologies, using STDIN.
	@args = ("valgrind", "./dyesub_backend", $id, $user, $title, $copies, $options);
	print join(":", @args) . "\n";
	run \@args, "<", "/tmp/${gp_name}.raw" or die("FAIL: backend $?: $row[0] $row[1] $row[2] $row[3] $row[4] $row[5]\n");
    }

	print "***** PASS\n";
}
exit($retval);
