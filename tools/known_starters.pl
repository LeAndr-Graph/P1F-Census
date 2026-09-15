#!/usr/bin/perl
# known_starters.pl -- write the six previously known P1Fs of K18 in run format.
#
#   perl tools/known_starters.pl > known.txt
#
# These are the six of the iso.18 list, the ones the literature had before this census. Each
# is given as a STARTER, a short list of pairs that is developed into all 17 rounds by adding
# a constant, and the two kinds develop differently:
#
#   'S'  starter in Z_17, 8 pairs. Vertices 0..16 plus one at infinity (17). Round z is the
#        starter shifted by z mod 17, together with the pair {z, infinity}. 17 rounds.
#   'E'  even starter in Z_16, 7 pairs. Vertices 0..15 plus two at infinity (16, 17). Round z
#        is the starter shifted by z mod 16, plus {z, 16} and {m+z, 17} where m is the one
#        nonzero element of Z_16 the starter does not use. That gives 16 rounds; the 17th is
#        the fixed round {i, i+8} for i = 0..7 together with {16, 17}.
#
# The starter data and the development are taken verbatim from an earlier Perl script
# (2026-07-27), which developed all six, checked each is a proper factorization of K18 -- 17
# rounds, 153 distinct edges -- counted their automorphisms independently, and found the six
# pairwise non-isomorphic.
#
# To name them: the matrices this writes are NOT in the labeling the engine emits, so they
# match nothing until they have been through the canonizer. Two steps:
#
#   perl tools/known_starters.pl > known.txt
#   REP_CANONFILE=known.txt RESULT=known_canon.txt p1f.exe 18 4
#   perl tools/compare_to_catalog.pl --index known_canon.txt
#
# Done on 2026-08-28, that reproduces the three catalog numbers already on record -- #10710,
# #9323 and #9527 -- which is the control, and names the other three. See AllResults\ReadMe.md
# section 5.2 for the result.
my $N = 18;

sub develop {
    my ($type, @v) = @_;
    my @F;
    if ($type eq 'S') {
        my @pairs; for (my $i=0;$i<@v;$i+=2){ push @pairs,[$v[$i],$v[$i+1]]; }
        for my $z (0..16) {
            my @adj=(-1)x$N;
            for my $p (@pairs){ my $a=($p->[0]+$z)%17; my $b=($p->[1]+$z)%17; $adj[$a]=$b;$adj[$b]=$a; }
            my $c=(0+$z)%17; $adj[$c]=17; $adj[17]=$c;
            push @F,\@adj;
        }
    } else {
        my @pairs; for (my $i=0;$i<@v;$i+=2){ push @pairs,[$v[$i],$v[$i+1]]; }
        my %in; for my $p (@pairs){ $in{$p->[0]}=1; $in{$p->[1]}=1; }
        my $m; for my $e (1..15){ if(!$in{$e}){ $m=$e; last; } }
        for my $z (0..15) {
            my @adj=(-1)x$N;
            for my $p (@pairs){ my $a=($p->[0]+$z)%16; my $b=($p->[1]+$z)%16; $adj[$a]=$b;$adj[$b]=$a; }
            my $a=(0+$z)%16; $adj[$a]=16; $adj[16]=$a;
            my $bb=($m+$z)%16; $adj[$bb]=17; $adj[17]=$bb;
            push @F,\@adj;
        }
        my @adj=(-1)x$N;
        for my $i (0..7){ my $j=$i+8; $adj[$i]=$j;$adj[$j]=$i; }
        $adj[16]=17;$adj[17]=16;
        push @F,\@adj;
    }
    return @F;
}

sub row {                      # one factor -> the run-format row: 9 pairs, u < v, by u
    my ($adj) = @_;
    my @out;
    for my $u (0..$N-1) { my $v = $adj->[$u]; push @out, $u, $v if $u < $v; }
    my $s = ' "';
    for my $i (0..$#out) { $s .= ($i % 2 ? ' ' : '  ') . sprintf('%2d', $out[$i]); }
    return $s . ' "';
}

my @order = (
  ['P1F01', 'S', 1,2,7,9,3,6,12,16,10,15,8,14,4,11,5,13],
  ['P1F09', 'S', 8,9,16,1,7,10,15,2,6,11,14,3,5,12,13,4],
  ['P1F18', 'E', 1,2,5,7,8,11,9,13,15,4,6,12,3,10],
  ['P1F20', 'E', 1,2,12,14,6,9,3,7,8,13,15,5,4,11],
  ['P1F21', 'E', 1,2,13,15,8,11,3,7,4,9,6,12,14,5],
  ['P1F23', 'E', 2,3,6,8,12,15,1,5,9,14,7,13,4,11],
);
my $seq = 0;
for my $r (@order) {
    my ($name, $type, @v) = @$r;
    my @F = develop($type, @v);
    die "$name: expected 17 factors, got " . scalar(@F) . "\n" unless @F == 17;
    $seq++;
    printf "#%d |Aut| = 0\n", $seq;          # |Aut| is recomputed by the canonizer
    print row($_), "\n" for @F;
}
warn "wrote $seq records, in the order " . join(', ', map { $_->[0] } @order) . "\n";
