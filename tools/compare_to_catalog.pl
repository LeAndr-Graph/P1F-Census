#!/usr/bin/perl
# compare_to_catalog.pl -- check what a run wrote against the archived catalog.
#
#   perl tools/compare_to_catalog.pl [--index] [--against <file>] <file> [<file> ...]
#
# AllResults\K18_P1F_aut_gt1.txt is the one catalog: every K18 P1F class known to have
# |Aut| > 1, each in the canonical form the engine itself emits. A record written by a run
# and the same class in the catalog are therefore byte-identical over their matrix rows,
# and that is what this compares -- the matrix, never the sequence number, which counts
# write order within one file and differs between two correct runs of the same case.
#
# It checks one thing: is every class the run wrote PRESENT in the catalog, under the same
# |Aut|. Ok or Fault, with a histogram either way. It does not check the other direction --
# what a run should have produced and did not. That needs the block a class belongs to and
# a reading of the log, and is deliberately left for later.
#
# --against replaces the reference, so the two files can be SWITCHED and the check run the
# other way round. That is a MANUAL TEST of this script, not part of a case run: the bats
# only ever run the forward direction, which is the one valid for any block range.
#
#   perl tools/compare_to_catalog.pl AllResults/K18-t9-a0.txt
#   perl tools/compare_to_catalog.pl --against AllResults/K18-t9-a0.txt AllResults/K18_P1F_aut_gt1.txt
#
# --index additionally names, for every record, WHICH record of the reference it is. Against
# the catalog that is the class's catalog number, the stable name for a class, which nothing
# else in the repo can tell you: a run's own #seq is write order and differs between two
# correct runs. Use it to identify a factorization that came from somewhere else, a paper or
# a construction, once it has been through REP_CANONFILE -- without which its matrix is in a
# different labeling and will match nothing.
#
# A file with no records is Ok: a block range that owns nothing writes nothing, which is
# the normal outcome for most ranges of a census.
#
# Exit code 0 = Ok, 1 = Fault.
use strict;
use warnings;
use File::Basename;
use File::Spec;

# The catalog lives beside this script's folder, not beside the bat that calls it, and every
# bat calls it from a different runs\ or regression\ folder. Naming it bare therefore reads
# as though it were local to the caller, which it never is: hence the path on every line that
# mentions it, repo-relative so it is the same string whatever folder the run was started from.
#
# ONE CATALOG PER N, named for what it actually holds -- the scope differs by N, and the name
# says so rather than making the reader look it up. K14's order-2 leg completes, so its catalog
# is the whole |Aut| > 1 classification; K16's does too, since 2026-09-01, when runs\K16Aut2
# harvested its 59 |Aut| = 2 classes and they were merged in; K18's |Aut| = 2 classes come from
# its three census cases, so it is |Aut| > 1 as well; only K20 still stops above an order-2
# search too big to run.
my %CATALOG = (
    14 => 'K14_P1F_aut_gt1.txt',
    16 => 'K16_P1F_aut_gt1.txt',   # was K16_P1F_aut_gt2.txt, 30 classes, until 2026-09-01
    18 => 'K18_P1F_aut_gt1.txt',
    20 => 'K20_P1F_aut_gt3.txt',
);
my $N = 18;   # --n picks the catalog. 18 is the default because every caller that predates
              # the other three means K18, and none of them passes the flag.

my ($index, $against, @files) = (0, undef);
while (@ARGV) {
    my $arg = shift @ARGV;   # not $a: it would shadow sort's $a in the --n message below
    # --against points the check at something that is not the catalog, so the messages stop
    # calling it one and echo the path as it was typed.
    if    ($arg eq '--against') { $against = shift @ARGV or die "--against needs a file\n" }
    elsif ($arg eq '--n')       { $N = shift @ARGV;
                                  defined $N && $CATALOG{$N}
                                      or die "--n takes one of: " . join(', ', sort { $a <=> $b } keys %CATALOG) . "\n" }
    elsif ($arg eq '--index')   { $index = 1 }
    else                        { push @files, $arg }
}
@files or die "usage: perl compare_to_catalog.pl [--index] [--n <N>] [--against <file>] <file> [...]\n";

my $CATREL  = File::Spec->catfile('AllResults', $CATALOG{$N});
my $REF     = File::Spec->catfile(dirname(dirname(File::Spec->rel2abs($0))), $CATREL);
my $refdesc = "the full catalog ($CATREL)";   # what the messages call it, path included
my $refword = 'catalog';                      # the same thing in one word, mid-sentence
if (defined $against) { $REF = $against; $refdesc = "the reference ($REF)"; $refword = 'reference' }

# A catalog that is not in the repository yet is NOT a fault in the run: the results were
# written and are correct, and only the look-up is impossible. Name the missing file, and exit
# 2 -- distinct from 1, so a bat can tell "could not check" from "checked, and wrong".
unless (-e $REF) {
    printf "\nCompare skipped: %s is not in the repository, so there is nothing to look up in.\n", $refdesc;
    printf "  The results are written and unaffected; only the check was skipped.\n";
    exit 2;
}

# ---- read a class file into (aut, matrix) records -----------------------------------
# Both layouts are read the same way: a "#<n> |Aut| = <k>" header followed by the rows up
# to the next header. The catalog's title, histogram and blank lines are simply not rows.
#
# A row is reduced to the NUMBERS on it. Column padding, how many spaces separate a pair,
# whether the row is wrapped in quotes at all -- none of it survives into the key, so two
# files that state the same factorization compare equal however either one lays it out.
# Only two things are still significant, and both are part of the answer rather than its
# formatting: the numbers, and the order of the rows within a record. Row order is fixed
# by the canonizer, so sorting it away would merge genuinely different canonical forms.
#
# The order of the RECORDS never mattered: they go into a hash and are looked up by key,
# which is why the catalog can be sorted and a run's output written in the order it found
# things.
sub read_records {
    my ($file) = @_;
    open my $h, '<', $file or die "cannot open $file: $!\n";
    my (@rec, $cur);
    while (my $line = <$h>) {
        $line =~ s/\r?\n\z//;
        if ($line =~ /^#(\d+)\s*\|Aut\|\s*=\s*(\d+)/) {
            push @rec, $cur if $cur;
            $cur = { seq => $1, aut => $2, rows => [] };
            next;
        }
        next unless $cur;
        # Any line carrying a digit is a row of the record being read: after a header there
        # is nothing else until the next one. Blank and prose lines have no digits and drop
        # out here; the file's preamble is already excluded by the test above.
        push @{ $cur->{rows} }, join(' ', $line =~ /(\d+)/g) if $line =~ /\d/;
    }
    push @rec, $cur if $cur;
    close $h;
    $_->{key} = join('|', @{ $_->{rows} }) for @rec;
    return @rec;
}

my (%ref, %refseq);                           # matrix -> |Aut|, and -> its record number
{
    my @r = read_records($REF);
    @r or die "reference $REF holds no records\n";
    for my $x (@r) { $ref{ $x->{key} } = $x->{aut}; $refseq{ $x->{key} } = $x->{seq} }
}
# basename() leaves a Windows path intact under a POSIX perl, and RESULT can arrive either
# way, so cut at the last separator of either kind. chr(92) is the backslash. Result files are
# shortened -- they ARE local to the bat -- and the catalog is not; that difference is the point.
sub shortname {
    my ($f) = @_;
    my $i = rindex($f, '/');
    my $j = rindex($f, chr(92));
    my $k = $i > $j ? $i : $j;
    return $k >= 0 ? substr($f, $k + 1) : $f;
}

sub row2 { printf "%10s%10s\n", @_ }
sub row3 { printf "%10s%10s%14s\n", @_ }

my $rc = 0;
for my $file (@files) {
    my @rec = read_records($file);
    my $name = shortname($file);
    print "\n";
    unless (@rec) {
        printf "Compare Ok: %s holds no results -- nothing to look up in %s\n", $name, $refdesc;
        next;
    }

    my (%present, %absent, @mismatch);
    for my $r (@rec) {
        my $have = $ref{ $r->{key} };
        if (defined $have && $have == $r->{aut}) { $present{ $r->{aut} }++ }
        else {
            $absent{ $r->{aut} }++;
            push @mismatch, [$r->{aut}, $have] if defined $have;
        }
    }
    my $nfound = 0; $nfound += $_ for values %present;
    my $nmiss  = 0; $nmiss  += $_ for values %absent;

    if ($index) {          # which record of the reference each one is
        printf "%s, record by record against %s\n\n", $name, $refdesc;
        # The verdict is the only column whose width is not known in advance: "#4442" is 5
        # characters, "not present" is 11, and "#4888, |Aut| 272" is 16 -- past the 14 this
        # used to reserve, which ran the verdict into the |Aut| column beside it. A bigger
        # catalog makes it longer still, so measure the column rather than pick a number that
        # the next catalog would silently overflow again.
        my @verdict = map {
            my $at   = $refseq{ $_->{key} };
            my $have = $ref{ $_->{key} };
            !defined $at        ? 'not present'
          : $have != $_->{aut}  ? "#$at, |Aut| $have"
          :                       "#$at";
        } @rec;
        my $w = length 'in ref';
        for (@verdict) { $w = length if length > $w }
        $w += 2;           # two spaces of gap, so the widest verdict never touches |Aut|
        printf "%10s%10s%*s\n", 'record', '|Aut|', $w, 'in ref';
        printf "%10s%10s%*s\n", '#' . $rec[$_]{seq}, $rec[$_]{aut}, $w, $verdict[$_] for 0 .. $#rec;
        print "\n";
    }

    if (!$nmiss) {
        printf "Compare Ok: all %d results of %s present in %s\n\n", scalar(@rec), $name, $refdesc;
        row2('|Aut|', 'results');
        row2($_, $present{$_}) for sort { $a <=> $b } keys %present;
        row2('total', scalar @rec);
        next;
    }

    $rc = 1;
    printf "Compare Fault: can't find %d of %d results of %s in %s\n\n",
           $nmiss, scalar(@rec), $name, $refdesc;
    row3('|Aut|', 'present', 'not present');
    my %seen = map { $_ => 1 } (keys %present, keys %absent);
    row3($_, $present{$_} || 0, $absent{$_} || 0) for sort { $a <=> $b } keys %seen;
    row3('total', $nfound, $nmiss);
    # A record whose matrix IS in the reference but under a different |Aut| is a different
    # kind of wrong from one the reference has never seen, so it is named separately. The path
    # is not repeated here -- the Fault line right above already carries it -- and each pair
    # says which side each number came from, because "2 vs 4" does not. Joined with ';' since
    # the pairs themselves contain a comma.
    printf "\n  %d record(s) match a %s class but disagree on |Aut| (%s)\n",
           scalar @mismatch, $refword,
           join('; ', map { "run $_->[0], $refword $_->[1]" } @mismatch) if @mismatch;
}
print "\n";
exit $rc;
