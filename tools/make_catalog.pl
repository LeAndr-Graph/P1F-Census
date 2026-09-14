#!/usr/bin/perl
# make_catalog.pl -- build an AllResults catalog from one or more RESULT files.
#
#   perl tools/make_catalog.pl --n <N> --scope "<text>" <out> <result> [<result> ...]
#
# The catalog is the archived answer a later run is checked against by
# compare_to_catalog.pl. It is the same records a run writes -- "#<n> |Aut| = <k>" followed
# by the rows of the canonical form -- with three differences:
#
#   * DEDUPED. A class found by two legs is written once per leg in a RESULT file (that is
#     what `Results = Saved + Duplicates` reports); a catalog holds each class once.
#   * SORTED, ascending lexicographic on the canonical form itself. A run writes in the order
#     it happened to find things, which differs between two correct runs; the catalog needs a
#     stable order so a diff between two versions is meaningful.
#   * HEADED, with the total and the |Aut| census, so the file states what it holds.
#
# Record numbers are POSITIONS in the sorted list, not names: they move if the catalog is ever
# rebuilt with more classes. Cite a class by its matrix, or by catalog version plus number.
#
# This mirrors the K18 catalog, which was built by an untracked script in another repository
# -- see the "no tracked recipe" note in AllResults/ReadMe.md. Nothing here is K18-specific.
use strict;
use warnings;

my ($N, $scope, @args);
while (@ARGV) {
    my $arg = shift @ARGV;
    if    ($arg eq '--n')     { $N     = shift @ARGV }
    elsif ($arg eq '--scope') { $scope = shift @ARGV }
    else                      { push @args, $arg }
}
my ($out, @files) = @args;
die "usage: perl make_catalog.pl --n <N> --scope <text> <out> <result> [...]\n"
    unless defined $N && defined $scope && defined $out && @files;
my $ROWS = $N - 1;   # a P1F of K_N has N-1 factors, so a record has N-1 rows

# ---- read the records -----------------------------------------------------------------
# The same reader compare_to_catalog.pl uses: a "#<n> |Aut| = <k>" header, then every quoted
# line until the next header. Anything else -- a title, a census table, a blank -- is not a row.
my @entries;
for my $f (@files) {
    open my $h, '<', $f or die "cannot open $f: $!\n";
    my ($aut, @rows);
    while (my $line = <$h>) {
        $line =~ s/\r?\n\z//;
        if ($line =~ /^#\d+\s*\|Aut\|\s*=\s*(\d+)/) {
            push @entries, [$aut, [@rows]] if @rows;
            ($aut, @rows) = ($1);
            next;
        }
        push @rows, $1 if defined $aut && $line =~ /^\s*"(.*)"\s*$/;
    }
    push @entries, [$aut, [@rows]] if @rows;
    close $h;
}
@entries or die "no records found in: @files\n";

# ---- dedup on the canonical form, then sort on it --------------------------------------
my (%seen, @keyed);
for my $e (@entries) {
    my ($aut, $rows) = @$e;
    die "a record has ", scalar(@$rows), " rows, expected $ROWS -- is --n $N right?\n"
        unless @$rows == $ROWS;
    my $key = join("\n", @$rows);
    # A repeat must agree on |Aut|. If it does not, two different classes share a canonical
    # form, which is a canonizer fault and not something to quietly collapse.
    if (exists $seen{$key}) {
        die "same canonical form under two different |Aut| ($seen{$key} and $aut)\n"
            unless $seen{$key} == $aut;
        next;
    }
    $seen{$key} = $aut;
    push @keyed, [$key, $aut, $rows];
}
@keyed = sort { $a->[0] cmp $b->[0] } @keyed;

my %census;
$census{ $_->[1] }++ for @keyed;
my $total = scalar @keyed;

# ---- write ------------------------------------------------------------------------------
open my $o, '>', $out or die "cannot write $out: $!\n";
print  $o "K$N complete graph factorization: $scope\n\n";
printf $o "Total: %d\n\n", $total;
printf $o "     |Aut|   classes\n";
printf $o "     %5d   %7d\n", $_, $census{$_} for sort { $a <=> $b } keys %census;
print  $o "\n";
print  $o "Each factorization is listed once, in the canonical form produced by the project\n";
print  $o "canonizer. Entries are sorted in ascending lexicographic order of that form.\n\n\n";
my $n = 0;
for my $e (@keyed) {
    my (undef, $aut, $rows) = @$e;
    printf $o "#%d |Aut| = %d\n", ++$n, $aut;
    print  $o " \"$_\"\n" for @$rows;
    print  $o "\n";
}
close $o;

printf "records read : %d\n", scalar @entries;
printf "duplicates   : %d\n", scalar(@entries) - $total;
printf "written      : %d -> %s\n", $total, $out;
printf "census       : %s\n", join(', ', map { "$_:$census{$_}" } sort { $a <=> $b } keys %census);
