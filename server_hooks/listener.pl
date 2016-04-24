#!/usr/bin/perl -w

# Perl script to listen for JSON pushes from LES-ORF systems
# This script accepts a JSON string and dumps it to a text file.  You can
# easily modify this script to append to a file or connect it to a SQL database.

use CGI;
use strict;
use warnings;
use CGI::Carp qw(warningsToBrowser fatalsToBrowser);
use lib 'JSON-2.90/lib';
use JSON;
use JSON qw(decode_json);

# Sniff out full JSON package
my $query = new CGI;
my $pkg   = $query->param("push");

# Decode the entire JSON
my $decoded_json = decode_json($pkg);

# Get the values from the JSON hash
my $data_mode = $decoded_json->{'object'};

my $filename = 'dump.txt';
open(my $fh, '>', $filename) or die "Could not open file '$filename' $!";
print $fh $pkg;
close $fh;


# Send confirmation back. Not really needed though
my $cgi = CGI->new();
print $cgi->header();
print "Success";

# All done!
