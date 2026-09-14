#!/usr/bin/perl
# k1717_count.pl -- how many P1Fs of K_{2n-1,2n-1} a catalogue yields by the K-construction,
#                   and from which roots.
#
#   perl tools/k1717_count.pl [--out <file>] [--only <k>[,<k>...]] [--k10] <catalogue>
#
# For every record F of the catalogue (a "#<k> |Aut| = <a>" header followed by the factor
# rows, read exactly as compare_to_catalog.pl reads them) this computes the automorphism
# group Aut(F) as an explicit list of vertex permutations, checks its order against the
# header, and counts the ORBITS of Aut(F) on the vertices.  By Laufer's theorem every root
# gives a perfect 1-factorization of K_{2n-1,2n-1}, and by Wanless-Ihrig (J. Combin. Des. 13
# (2005), Theorem 16) two roots give the same main class exactly when an automorphism maps
# one to the other, while distinct classes never collide.  So
#
#     #(P1Fs of K_{2n-1,2n-1} from the catalogue)  =  SUM over records of  #orbits(Aut(F)),
#
# with the one proviso the spec explains: an ATOMIC square can add up to two more bipartite
# classes from its other conjugates.  That case is handled by the atomic scan, not here.
#
# Every record is first re-verified as a P1F (every pair of factors one Hamiltonian cycle);
# a failure, or an |Aut| that disagrees with the header, stops the run with a message.
#
# Output: the console gets a table by |Aut| stratum (classes, orbit-count histogram, stratum
# sum) and the grand total.  --out writes a file in the catalogue's own shape -- title, Total,
# histogram, explanation, then one entry per record in catalogue order, "#<k> |Aut| = <a>
# orbits = <o> roots = <r ...>", the roots being the smallest vertex of each orbit so that
# (record, root) names every K_{2n-1,2n-1} P1F exactly once.
# --only restricts the run to the listed record numbers (a quick check of one class).
# --k10 ignores the catalogue argument, builds the unique P1F of K10 by search, and reports
# its orbit count: the control from Wanless-Ihrig, "only one P1F of K9,9 comes from K10",
# so the answer must be 1.
#
# Cost: the automorphism search fixes the images of vertices 0 and 1 (2n(2n-1) choices) and
# then forces almost everything through the induced map on factors; a K18 record takes a
# few milliseconds, the whole 10,710-record catalogue some minutes.
use strict;
use warnings;

my ($out, $only, $k10);
my @files;
while (my $arg = shift @ARGV) {
    if    ($arg eq '--out')  { $out  = shift @ARGV or die "--out needs a file\n" }
    elsif ($arg eq '--only') { $only = shift @ARGV or die "--only needs record numbers\n" }
    elsif ($arg eq '--k10')  { $k10  = 1 }
    elsif ($arg =~ /^-/)     { die "unknown option $arg\n" }
    else                     { push @files, $arg }
}
@files or $k10 or die "usage: perl k1717_count.pl [--out <file>] [--only k,k,...] [--k10] <catalogue>\n";
my %only = map { $_ => 1 } split /,/, ($only // '');

my $OUT;
if (defined $out) { open $OUT, '>', $out or die "cannot write $out: $!\n" }
sub say_ { my $s = join('', @_); print $s; print $OUT $s if $OUT }

# ---- read a class file into (seq, aut, rows) records -- same reading as compare_to_catalog.pl
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
        push @{ $cur->{rows} }, [ $line =~ /(\d+)/g ] if $line =~ /\d/;
    }
    push @rec, $cur if $cur;
    close $h;
    return @rec;
}

# ---- factor tables: fac[u][v] = factor of edge uv, part[f][u] = partner of u in factor f
sub build_tables {
    my ($rows) = @_;
    my $m = @$rows;                       # factors
    my $N = $m + 1;                       # vertices
    my (@fac, @part);
    for my $f (0 .. $m - 1) {
        my $r = $rows->[$f];
        @$r == $N or die "row $f has " . scalar(@$r) . " numbers, expected $N\n";
        my %seen;
        for (my $i = 0; $i < $N; $i += 2) {
            my ($u, $v) = ($r->[$i], $r->[$i + 1]);
            ($u < $N && $v < $N && $u != $v) or die "row $f: bad pair $u $v\n";
            !$seen{$u}++ && !$seen{$v}++ or die "row $f: vertex used twice\n";
            !defined $fac[$u][$v] or die "edge {$u,$v} in two factors\n";
            $fac[$u][$v] = $fac[$v][$u] = $f;
            $part[$f][$u] = $v; $part[$f][$v] = $u;
        }
    }
    for my $u (0 .. $N - 1) { for my $v ($u + 1 .. $N - 1) { defined $fac[$u][$v] or die "edge {$u,$v} in no factor\n" } }
    return ($N, $m, \@fac, \@part);
}

# ---- perfection: every pair of factors is one Hamiltonian cycle
sub is_p1f {
    my ($N, $m, $part) = @_;
    for my $f (0 .. $m - 2) { for my $g ($f + 1 .. $m - 1) {
        my ($x, $len) = (0, 0);
        do { $x = $part->[$g][ $part->[$f][$x] ]; $len += 2 } until $x == 0;
        return 0 if $len != $N;
    } }
    return 1;
}

# ---- Aut(F): all vertex permutations mapping factors to factors
sub automorphisms {
    my ($N, $m, $fac, $part) = @_;
    my @auts;
    my @g = (-1) x $N; my @used = (0) x $N;
    my @phi = (-1) x $m; my @phiinv = (-1) x $m;     # induced map on factors, and its inverse
    my $assign;
    $assign = sub {
        my ($v) = @_;
        if ($v == $N) { push @auts, [@g]; return }
        # one known factor image at v forces g[v]; otherwise every unused vertex is a candidate
        my @cands;
        for my $u (0 .. $v - 1) {
            my $pf = $phi[ $fac->[$u][$v] ];
            if ($pf >= 0) { @cands = ($part->[$pf][ $g[$u] ]); last }
        }
        @cands = grep { !$used[$_] } 0 .. $N - 1 unless @cands;
        for my $x (@cands) {
            next if $used[$x];
            my (@set, $ok); $ok = 1;
            for my $u (0 .. $v - 1) {
                my $f = $fac->[$u][$v]; my $f2 = $fac->[ $g[$u] ][$x];
                if ($phi[$f] >= 0) { if ($phi[$f] != $f2) { $ok = 0; last } }
                elsif ($phiinv[$f2] >= 0) { $ok = 0; last }
                else { $phi[$f] = $f2; $phiinv[$f2] = $f; push @set, $f }
            }
            if ($ok) { $g[$v] = $x; $used[$x] = 1; $assign->($v + 1); $used[$x] = 0; $g[$v] = -1 }
            for my $f (@set) { $phiinv[ $phi[$f] ] = -1; $phi[$f] = -1 }
        }
    };
    $assign->(0);
    return \@auts;
}

# ---- orbits of a list of permutations on 0..N-1: returns [root, size] per orbit, by root,
#      where the root is the smallest vertex of the orbit -- the representative that names
#      the K-construction square of that orbit
sub orbits {
    my ($N, $auts) = @_;
    my @p = (0 .. $N - 1);
    my $find; $find = sub { my $x = shift; $p[$x] = $find->($p[$x]) if $p[$x] != $x; return $p[$x] };
    for my $a (@$auts) { for my $x (0 .. $N - 1) { my ($r, $s) = ($find->($x), $find->($a->[$x])); $p[$r] = $s if $r != $s } }
    my (%size, %root);
    for my $x (0 .. $N - 1) { my $r = $find->($x); $size{$r}++; $root{$r} = $x if !defined $root{$r} || $x < $root{$r} }
    return map { [ $root{$_}, $size{$_} ] } sort { $root{$a} <=> $root{$b} } keys %size;
}

# ---- one record -> N, |Aut|, and its orbits
sub process {
    my ($seq, $auth, $rows) = @_;
    my ($N, $m, $fac, $part) = build_tables($rows);
    is_p1f($N, $m, $part) or die "record #$seq is NOT a perfect 1-factorization -- stop\n";
    my $auts = automorphisms($N, $m, $fac, $part);
    my $a = @$auts;
    (!defined $auth || $a == $auth) or die "record #$seq: computed |Aut| = $a, header says $auth -- stop\n";
    my @orb = orbits($N, $auts);
    my @sizes = sort { $b <=> $a } map { $_->[1] } @orb;
    return ($N, $a, scalar(@orb), \@sizes, [ map { $_->[0] } @orb ]);
}

# ---- --k10: the unique P1F of K10, found by search, as the external control
if ($k10) {
    my $N = 10;
    my @matchings;                                  # all 945 perfect matchings of K10, as pair lists
    my $gen; $gen = sub {
        my ($free, $acc) = @_;
        if (!@$free) { push @matchings, [@$acc]; return }
        my ($u, @rest) = @$free;
        for my $i (0 .. $#rest) { my @r2 = @rest; my ($v) = splice @r2, $i, 1; $gen->(\@r2, [@$acc, $u, $v]) }
    };
    $gen->([0 .. $N - 1], []);
    my @chosen; my $found;
    my $perfect_with = sub {                        # matching a (pair list) perfect with matching b
        my ($a, $b) = @_;
        my (@pa, @pb);
        for (my $i = 0; $i < $N; $i += 2) { $pa[$a->[$i]] = $a->[$i+1]; $pa[$a->[$i+1]] = $a->[$i]; $pb[$b->[$i]] = $b->[$i+1]; $pb[$b->[$i+1]] = $b->[$i] }
        my ($x, $len) = (0, 0); do { $x = $pb[$pa[$x]]; $len += 2 } until $x == 0;
        return $len == $N;
    };
    my @covered;
    my $dfs; $dfs = sub {
        return 1 if @chosen == $N - 1;
        my ($u0, $v0);                              # lowest uncovered edge
        OUTER: for my $u (0 .. $N - 1) { for my $v ($u + 1 .. $N - 1) { if (!$covered[$u][$v]) { ($u0, $v0) = ($u, $v); last OUTER } } }
        for my $M (@matchings) {
            my $has = 0; my $clash = 0;
            for (my $i = 0; $i < $N; $i += 2) { my ($u, $v) = ($M->[$i], $M->[$i+1]); $has = 1 if $u == $u0 && $v == $v0; $clash = 1 if $covered[$u][$v] }
            next unless $has && !$clash;
            next if grep { !$perfect_with->($M, $_) } @chosen;
            for (my $i = 0; $i < $N; $i += 2) { $covered[$M->[$i]][$M->[$i+1]] = 1 }
            push @chosen, $M;
            return 1 if $dfs->();
            pop @chosen;
            for (my $i = 0; $i < $N; $i += 2) { $covered[$M->[$i]][$M->[$i+1]] = 0 }
        }
        return 0;
    };
    $dfs->() or die "no P1F of K10 found -- the search is wrong\n";
    my ($n, $a, $orb, $sizes, $roots) = process('K10', undef, \@chosen);
    say_(sprintf "K10 control: the P1F of K10 has |Aut| = %d, %d vertex orbit(s) (sizes %s) -> %d P1F(s) of K9,9 by the K-construction; Wanless-Ihrig say 1.\n",
         $a, $orb, join('+', @$sizes), $orb);
    say_(($orb == 1 ? "K10 control PASSED\n" : "K10 control FAILED\n"));
    exit($orb == 1 ? 0 : 1) unless @files;
}

# ---- the catalogue.  The console gets the summary; --out gets a file in the catalogue's own
#      shape: title, Total, histogram, an explanation, then the records in catalogue order.
for my $file (@files) {
    my @rec = read_records($file);
    @rec or die "$file holds no records\n";
    my (%classes, %sum, %hist, %t8, %t9, @lines);
    my ($total, $count, $Nseen) = (0, 0, undef);
    print "k1717_count: $file\n";
    for my $r (@rec) {
        next if %only && !$only{ $r->{seq} };
        my ($N, $a, $orb, $sizes, $roots) = process($r->{seq}, $r->{aut}, $r->{rows});
        $Nseen //= $N;
        push @lines, sprintf "#%d |Aut| = %d  orbits = %d  roots = %s\n", $r->{seq}, $a, $orb, join(' ', @$roots);
        $classes{$a}++; $sum{$a} += $orb; $hist{$a}{$orb}++; $total += $orb; $count++;
        if ($a == 2) { my $fixed = grep { $_ == 1 } @$sizes; $fixed ? $t8{$a}++ : $t9{$a}++ }
    }
    my $n1 = $Nseen - 1;
    (my $src = $file) =~ s{.*[\\/]}{};

    # -- the file, catalogue-shaped
    if ($OUT) {
        print $OUT "K$n1,$n1 perfect 1-factorizations obtained from the K$Nseen catalogue by the K-construction\n\n";
        print $OUT "Total: $total\n\n";
        printf $OUT "%10s %9s %13s\n", '|Aut|', 'classes', "K$n1,$n1 P1Fs";
        printf $OUT "%10d %9d %13d\n", $_, $classes{$_}, $sum{$_} for sort { $a <=> $b } keys %classes;
        print $OUT "\n";
        print $OUT "|Aut| is the automorphism-group order of the K$Nseen class, as in $src, whose\n";
        print $OUT "record numbers are used unchanged. Every class contributes one P1F of K$n1,$n1 per orbit\n";
        print $OUT "of its automorphism group on the vertices (Laufer 1980; Wanless-Ihrig, J. Combin. Des.\n";
        print $OUT "13 (2005), Theorem 16): the K-construction square I(F, j) at a root j, and roots in one\n";
        print $OUT "orbit give the same square up to paratopy. \"roots\" lists the smallest vertex of each\n";
        print $OUT "orbit, so (record, root) names each K$n1,$n1 P1F exactly once. Records are in the\n";
        print $OUT "catalogue's order, ascending lexicographic order of the canonical form.\n\n";
        print $OUT $_ for @lines;
    }

    # -- the console summary
    print "\n";
    printf "%-6s %-8s %-10s %s\n", '|Aut|', 'classes', 'sum', 'orbit-count histogram';
    for my $ord (sort { $a <=> $b } keys %classes) {
        my $h = join(' ', map { "$_:$hist{$ord}{$_}" } sort { $a <=> $b } keys %{ $hist{$ord} });
        printf "%-6d %-8d %-10d %s\n", $ord, $classes{$ord}, $sum{$ord}, $h;
    }
    printf "%-6s %-8d %-10d\n", 'total', $count, $total;
    if ($classes{2}) {
        my ($t8, $t9) = ($t8{2} // 0, $t9{2} // 0);
        printf "\n|Aut| = 2 arithmetic: %d two-fixed classes x %d orbits + %d fixed-point-free x %d orbits = %d (stratum sum %d)\n",
             $t8, $Nseen / 2 + 1, $t9, $Nseen / 2, $t8 * ($Nseen / 2 + 1) + $t9 * ($Nseen / 2), $sum{2};
    }
    printf "\nBy the K-construction these %d classes yield %d pairwise non-isomorphic perfect 1-factorizations of K%d,%d,\n", $count, $total, $n1, $n1;
    print "one per Aut-orbit of roots (Laufer 1980; Wanless-Ihrig 2005, Thm 16), before any atomic-square correction.\n";
    print "written: $out\n" if $OUT;
}
close $OUT if $OUT;
