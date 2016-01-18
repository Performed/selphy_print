#!/usr/bin/perl -w
##################

# This script converts the D70 family's 'CPC' correction tables into
# C headers for the selphy_print backend.

use strict;
use Text::CSV;

my $csv = Text::CSV->new();
my @rows;

my $fname = $ARGV[0];

$fname =~ s/(.*).cpc/$1/;

open my $fh, "<$fname.cpc" or die "Can't open file: $!";
while (my $row = $csv->getline($fh)) {
    push @rows, $row;
}
close $fh;

my $ref = shift(@rows);
my @names = @$ref;
$ref = shift(@rows);
my @lens = @$ref;

my @cols;

for (my $i = 1 ; $i < scalar @names ; $i++) {
    my $col = $names[$i];
    $col =~ tr/[]()* //d;
    $col =~ tr/A-Z/a-z/;
    $col =~ s/yb/by/;
    $col =~ s/mg$/gm/;
    $col =~ s/cr/rc/;
    $names[$i] = $col;
    $cols[$i] = [ ];
}

if (scalar @names < 20){ # Not present on D70/D707.  All printers identical, Can fill in with defaults.
    $names[19] = "rolk";
    $lens[19] = 13;
}
if (scalar @names < 21){ # Not present on ASK300 or D70/D707. Only some D80 different, can fill in with defaults.
    $names[20] = "rev";
    $lens[20] = 76;
}

foreach $ref (@rows) {
    my @row = @$ref;
    for (my $i = 1; $i < scalar(@row) ; $i++) {
	push(@{$cols[$i]}, $row[$i]);
    }
}

open $fh, ">$fname.h" or die "Can't open file: $!";
print $fh "#include <stdint.h>\n";
print $fh "\n\n";

print $fh "#ifndef CORRDATA_DEF\n";
print $fh "#define CORRDATA_DEF\n";

print $fh "struct mitsu70x_corrdata {\n";
for (my $i = 1 ; $i < scalar(@names) ; $i++) {
    my $type = "double";
    if ($i <= 6 || $i == 15 || $i == 19) {
	$type = "uint16_t";
    } elsif ($i == 20) {
	$type = "uint32_t";
    }

    print $fh "\t$type " . $names[$i] . "[$lens[$i]];\n";
}
print $fh "};\n";

print $fh "\n\n";

print $fh "struct mitsu70x_corrdatalens {\n";

for (my $i = 1 ; $i < scalar(@names) ; $i++) {
    print $fh "\tsize_t " . $names[$i] . ";\n";
}
print $fh "};\n";

print $fh "\n\n";

print $fh "#endif\n";

print $fh "\n\n";

print $fh "static struct mitsu70x_corrdatalens ${fname}_lengths = {\n";

for (my $i = 1 ; $i < scalar(@names) ; $i++) {
    print $fh "\t." . $names[$i] . " = $lens[$i],\n";
}

print $fh "};\n";

print $fh "\n\n";

print $fh "static struct mitsu70x_corrdata ${fname}_data = {\n";

for (my $i = 1 ; $i < scalar(@names) ; $i++) {
    if (exists $cols[$i]) {
        print $fh "\t." . $names[$i] . " = {";
	for (my $j = 0 ; $j < $lens[$i] ; $j++) {
	    print $fh " $cols[$i][$j],";
	}
	print $fh " },\n";	
    }
}

print $fh "};\n";

close $fh;




