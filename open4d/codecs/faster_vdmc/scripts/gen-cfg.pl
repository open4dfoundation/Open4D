#!/usr/bin/env perl

use 5.022;
use Digest::MD5;
use File::Path qw(make_path);
use File::Basename qw(basename);
use Getopt::Long;
use List::Util qw{pairmap};
use List::MoreUtils qw{firstres};
use Module::Load::Conditional qw(can_load);
use Pod::Usage;
use YAML '0.50';
use strict;

=head1 NAME

gen-cfg.pl - Generate experiment configuration from yaml specification

=head1 SYNOPSIS

gen-cfg.pl [options] [yaml-config-spec ...]

=head1 OPTIONS

=over 4

=item B<--prefix>=dir

Sets the output path for the generated configuration tree.

=item B<--output-src-glob-sh>

=item B<--no-output-src-glob-sh> (default)

Do not generate src-glob.sh files describing source locations

=item B<--skip-sequences-without-src> (default)

=item B<--no-skip-sequences-without-src>

Do not generate configuration files for sequences that have an empty 'src'
field in the yaml specification.  This option is permits a later yaml spec
to effectively remove a sequence from being used in an experiment.

It may be useful to disable this option when generating config files when
the source location of the input data is not known.

=item B<--only-seqs>=seq1[:seq2][:...]

Generate configuration files for only the named sequences.

=item B<--exclude-seqs>=seq1[:seq2][:...]

Do not generate configuration files for the named sequences.

=item B<--batch-template>=script.tt

Generate a script from the template script.tt.  The output is written to
the current working directory.

=item B<--experiment-name>=name

A generic name that may be referenced by a batch template for any purpose.

=back

=head1 Config specification files



=cut


##
# Command line processing
my $do_help = '';
my $output_src_glob_sh = 0;
my $skip_sequences_without_src = 1;
my $experiment_name = '';
my $batch_template = '';
my $prefix = '.';
my $only_seqs = '';
my $exclude_seqs = '';
GetOptions(
	'help' => \$do_help,
	'prefix=s' => \$prefix,
	'output-src-glob-sh!' => \$output_src_glob_sh,
	'skip-sequences-without-src!' => \$skip_sequences_without_src,
	'batch-template=s' => \$batch_template,
	'experiment-name=s' => \$experiment_name,
	'only-seqs=s' => \$only_seqs,
	'exlude-seqs=s' => \$exclude_seqs,
);

##
# display help text and exit if asked, or if no config is provided
pod2usage(0) if $do_help;
pod2usage(1) unless @ARGV;

# sanitise command line arguments
my @only_seqs = split /:/, $only_seqs;
my @exclude_seqs = split /:/, $exclude_seqs;

##
# load all yaml snippets and merge into a single description
#  - disable automatic blessing in YAML >= 1.25
#  - fallback to automatic blessing in YAML <= 1.24
#    NB: yaml < 1.24 cannot use yaml_load with inline scalars
#
$YAML::LoadBlessed = 0;
$YAML::TagClass->{conditional} = 'conditional';
delete $conditional::{yaml_load} unless eval { YAML->VERSION(1.24) } ;
my @origins = @ARGV;
my %cfg;
while (@ARGV) {
	my $fname = shift @ARGV;
	my $cfg = YAML::LoadFile($fname) or die "$fname: $!";
	merge(\%cfg, $cfg);
}

##
# dump the merged configuration (allows reproduction)
#
YAML::DumpFile("$prefix/config-merged.yaml", \%cfg);

##
# generate encoder/decoder configuration files
#

# list of configured jobs
my @jobs;

# this just makes later code look simpler
my $cfg = \%cfg;

# iterate over each configuration and described sequences
foreach my $cat_name (sort keys %{$cfg->{categories}}) {
	my $cat = $cfg->{categories}{$cat_name};

	foreach my $seq_name (sort keys %{$cat->{sequences}}) {
		next if @only_seqs && !grep {$_ eq $seq_name} @only_seqs;
		next if grep {$_ eq $seq_name} @exclude_seqs;

		my $cat_seq = $cat->{sequences}{$seq_name};
		my $seq = $cfg->{sequences}{$seq_name};
		my $cond_id = $cfg->{categories}{$cat_name}->{'cond-id'};
		my $seq_id = $cat_seq->{'seq-id'};
		unless (exists $seq->{gops}) {
			genSeqVariants($cat, $cat_name, $cond_id, $cat_seq,  $seq_name, $seq_id, $seq, $seq);
			next;
		}

		# split sequence into groups of pictures for parallel execution
		my $gop_idx = 0;
		foreach my $gop (@{$seq->{gops}}) {
			my $gop_idx = sprintf "%03d", $gop_idx++;
			my $gop_name = "${seq_name}_gop${gop_idx}";
			genSeqVariants($cat, $cat_name, $cond_id, $cat_seq, $gop_name, $seq_id, $gop, $seq);
		}
	}
}

##
# write out batch-job configuration
#
if ($batch_template) {
	can_load(modules => {'Template' => undef})
		|| die $Module::Load::Conditional::ERROR;

	my $output = "$prefix/" . basename($batch_template,'.tt');

	my $vars = {
		jobs => \@jobs,
		experiment_name => $experiment_name,
	};

	my $tt = Template->new({
		RELATIVE => 1, # allow relative paths as $batch_template
		ABSOLUTE => 1, # allow absolute paths too
	}) || die "$Template::ERROR\n";
	$tt->process($batch_template, $vars, $output) || die $tt->error(), "\n";
}


sub genSeqVariants {
	my ($cat, $cat_name, $cond_id, $cat_seq, $seq_name, $seq_id, $gop, $seq) = @_;

	# if sequence source isn't defined at top level, skip
	if ($skip_sequences_without_src) {
		next unless defined $gop->{src};
	}

	# generate the list of variants (if any)
	my @variants = List::MoreUtils::uniq (
		# $cat.sequences.$name.$variant:
		(grep {
				my $ref = $cat_seq->{$_};
				ref $ref eq 'HASH'
				and (exists $ref->{encoder} || exists $ref->{decoder})
			} keys %$cat_seq),

		# $cat.sequences.$name.encoder[].$param.$variant:
		variants_from_node($cat_seq->{encoder}),

		# $cat.encoder[].$param.$variant
		variants_from_node($cat->{encoder}),

		# $seq.$name.encoder[].$param.$variant
		variants_from_node($seq->{encoder}),
	);

	# handle the case of no variants: single case with defaults
	push @variants, undef unless @variants;

	# for each variant, derive the encoder options
	#   NB: in the case of no variants, $var = undef
	foreach my $var (sort @variants) {

		#my $cfgdir =	join '/', grep {defined} ($prefix,$cat_name,$seq_name,$var);
		my $short_seq_name = substr($seq_name, 0, 4);
		my $cfgdir = $prefix . "/s" . $seq_id . "c" . $cond_id . $var . "_" . $short_seq_name ;		
			
		print "$cfgdir\n";
		make_path($cfgdir);
		push @jobs, "$cfgdir/";
		
		##
		# input sequence file name
		if ($gop->{src} && $output_src_glob_sh) {
			my $src_seq = join '/', grep {defined} (
				(List::MoreUtils::firstval {defined}
					$seq->{'base-dir'},
					$cfg->{'sequence-base-dir'}),
				$seq->{'src-dir'},
				$gop->{src},
			);

			open my $fd, ">", "$cfgdir/src-glob.sh";
			print $fd "$src_seq\n";
		}

		if ($gop->{norm} && $output_src_glob_sh) {
			my $norm_seq = join '/', grep {defined} (
				(List::MoreUtils::firstval {defined}
					$seq->{'base-norm-dir'},
					$seq->{'base-dir'},
					$cfg->{'sequence-base-dir'}),
				$seq->{'norm-dir'},
				$gop->{norm},
			);

			open my $fd, ">", "$cfgdir/norm-glob.sh";
			print $fd "$norm_seq\n";
		}

		##
		# make dictionary for any variable substitutions
		my $dict = dict_from_context($var, $cat_seq, $gop, $seq, $cfg->{vars} || {});

		##
		# per tool configuration
		foreach my $tool (@{$cat->{tools} || [qw{encoder decoder}]}) {
			# base tool configuration
			my @flags = (
				params_from_node($dict, $cfg->{tools}{$tool}),
				params_from_node($dict, $cat->{$tool}, $var),
			);

			# don't write out empty config files (remove them)
			my $is_empty = List::Util::all {
					is_empty_cfg_line($_) || is_comment_cfg_line($_)
				} @flags;

			# remove any out-of-date file that should be empty
			rm_cfg("$cfgdir/$tool.cfg") if $is_empty;
			next if $is_empty;

			# add sequence/general overrides
			push @flags,
				params_from_node($dict, $seq->{$tool}),
				params_from_node($dict, $cat_seq->{$tool}, $var),
				params_from_node($dict, $cat_seq->{$var}{$tool}),
				params_from_node($dict, $cfg->{$tool});

			write_cfg("$cfgdir/$tool.cfg", \@flags);
		}
	}
}


#############################################################################
# utilities

##
# keywise merge $src into $dst, following the following merge rules:
#  - *      -> undef  = copy
#  - scalar -> scalar = replace
#  - hash   -> hash   = recurse
#  - list   -> scalar = merge unique items (scalars only)
#  - list   -> list   = merge unique items (scalars only)
sub merge {
	my ($dst, $src) = @_;

	unless (defined $dst) {
		$$dst = $$src;
		return;
	}

	##
	# overwrite existing scalar
	unless (ref $src) {
		$$dst = $src;
		return;
	}

	if (ref $src eq 'HASH') {
		foreach my $key (keys %$src) {
			##
			# copy sub-tree if key does not exist
			unless (exists $$dst{$key}) {
				$$dst{$key} = $$src{$key};
				next;
			}

			##
			# recurse to merge sub-tree
			if (ref $$dst{$key}) {
				merge($$dst{$key}, $$src{$key});
			}
			else {
				merge(\$$dst{$key}, $$src{$key});
			}
		}
		return;
	}

	##
	# merge arrays
	#  -- this is really only for an array of scalars
	if (ref $src eq 'ARRAY') {
		my @vals;
		push @vals, $$dst if ref $dst eq 'SCALAR';
		push @vals, @$dst if ref $dst eq 'ARRAY';
		push @vals, @$src;

		$$dst = [List::MoreUtils::uniq(@vals)] if ref $dst eq 'SCALAR';
		@$dst = List::MoreUtils::uniq(@vals) if ref $dst eq 'ARRAY';
		return;
	}
}


sub variants_from_node {
	my ($node) = @_ or return ();
	return
		map {keys %$_}
		grep {ref $_ eq 'HASH'}
		map {values %$_}
		grep {ref $_ eq 'HASH'}
		map {ref $_ eq 'ARRAY' ? @$_ : $_}
		@{$node};
}

sub params_from_node {
	my ($dict, $node, $variant) = @_;
	return () unless $node;

	# add a key-value pair to the array of parameters, first evaluating
	# the value to expand any variable substitutions
	sub push_eval(+$$$) {
		my ($aref, $dict, $key, $value) = @_;
		die "Not an array or arrayref" unless ref $aref eq 'ARRAY';
		my ($evald, @substs) = eval_expr($value, $dict);

		if (0 && @substs) {
			# substitution happened => add comment annotations
			push @$aref,
				[""],
				["# $key = $value, with " . join ", ", pairmap {"$a = $b"} @substs];
		}

		push @$aref, [$key, $evald];
		();
	}

	my @params;
	my @todo = $node;
	while (my $item = shift @todo) {
		# an unformatted string (not key:value)
		unless (ref $item) {
			push @params, [$item];
			next;
		}
		if (ref $item eq 'HASH') {
			while (my ($key, $value) = each %$item) {
				unless (ref $value) {
					# key:value without variants
					push_eval @params, $dict, $key, $value;
					next;
				}
				if (ref $value eq 'HASH') {
					# key:value with variants
					push_eval @params, $dict, $key, $value->{$variant}
						if exists $value->{$variant};
					next;
				}
				warn "unhandled node for $value";
			}
		}
		if (ref $item eq 'ARRAY') {
			# if the first item of the array is a conditional, evaluate
			# it and conditionally add the array
			if (exists $item->[0] && ref $item->[0] eq 'conditional') {
				my ($evald, @subst) = eval_expr(${$item->[0]}, $dict);
				push @params, [""];
				unless (eval $evald) {
					push @params,
					["# skipped $#$item elements: '${$item->[0]}' is false"];
					push @params, substs_to_comments(\@subst), [""];
					next;
				}
			}

			unshift @todo, @$item;
			next;
		}
	}
	return @params;
}


##
# convert the substition list to an array of comment params,
# deduplicating substititions.
sub substs_to_comments {
	my ($subst) = @_;
	my %dup;
	my $joiner = "with";
	return pairmap {
		if (exists $dup{$a}) {
			()
		} else {
			my $txt = "#   $joiner $a = '$b'";
			$dup{$a} = ();
			$joiner = " and";
			[$txt]
		}
	} @$subst;
}


##
# Build a dictionary of all variables that apply to the current variant
# from the given context.
sub dict_from_context {
	my ($variant, @context) = @_;
	my %dict;

	# discover all variables with earlier contexts having priority
	foreach my $context (reverse @context) {
		while (my ($var, $val) = each %$context) {
			my $type = ref $val;
			if (!$type) {
				# scalar, applies to all variants
				$dict{$var} = $val;
			}
			if ($type eq 'HASH') {
				# see if variant is specified
				$dict{$var} = $val->{$variant} if exists $val->{$variant};
			}
		}
	}

	return \%dict;
}


##
# Return the exansion of $str given a dictionary of variables.
sub eval_expr {
	my ($str, $dict) = @_;
	my @substs;

	# first find all variables and substitute their values
	#  - substitute an empty string if not found
	while ($str =~ m/\$\{([^}]+)\}/gc) {
		my $var = $1;
		my $var_start = $-[0];
		my $var_len  = $+[0] - $-[0];

		my $subst = "(undef)";
		$subst = $dict->{$var} if exists $dict->{$var};

		substr $str, $var_start, $var_len, $subst;
		pos $str = $var_start + length($subst);

		push @substs, ($var, $subst);
	}

	# finally evaluate any eval expressions
	pos $str = 0;
	while ($str =~ m/\$eval\{([^}]+)\}/gc) {
		my $expr = $1;
		my $expr_start = $-[0];
		my $expr_len  = $+[0] - $-[0];
		my $val = eval "use POSIX qw{round signbit}; use List::Util qw{min max}; no strict; $expr";
		if ($@) { print STDERR "err: $@\n$expr\n"; }
		substr $str, $expr_start, $expr_len, $val;
		pos $str = $expr_start + length($val);
	}

	return ($str, @substs);
}


##
# Print configuration @$opts, to $fd; with one entry per line and where
# each entry in @$opts is either a [key, value] pair to be joined with
# ": ", or just [key].
sub print_cfg {
	my ($fd, $opts) = @_;

	print $fd "# This file was automatically generated from:\n";
	print $fd "#   $_\n" foreach (@origins);

	local $\ = "\n";
	foreach my $opt (@$opts) {
		print $fd join(": ", @$opt);
	}
}

## true if the config line is empty
sub is_empty_cfg_line {
	my $line = shift @_ or $_;
	return scalar @$line == 1 && $line->[0] eq "";
}

## true if the config line is a comment
sub is_comment_cfg_line {
	my $line = shift @_ or $_;
	return scalar @$line == 1 && $line->[0] =~ /^#/;
}


##
# print config to file iff it differs from file's contents.
# (ie, don't touch mtime if unchanged)
sub write_cfg {
	my ($filename, $flags) = @_;

	# remove leading / trailing empty lines
	shift @$flags while scalar @$flags && is_empty_cfg_line($$flags[0]);
	pop @$flags   while scalar @$flags && is_empty_cfg_line($$flags[-1]);

	# format config in memory
	my $new_cfg = "";
	open my $fd, ">:encoding(utf8)", \$new_cfg;
	print_cfg($fd, $flags);
	close $fd;

	# hash it
	my $md5_new = Digest::MD5->new;
	$md5_new->add($new_cfg);

	my $md5_old = Digest::MD5->new;
	if (-f $filename) {
		open $fd, "<:encoding(utf8)", $filename;
		$md5_old->addfile($fd);
		close $fd;
	}

	if ($md5_new->digest ne $md5_old->digest) {
		print "writing $filename\n";
		open $fd, ">:bytes", $filename;
		print $fd $new_cfg;
		close $fd;
	}
}


##
# remove config
sub rm_cfg {
	my ($filename) = @_;

	print "removed $filename\n" if -f $filename;
	unlink $filename;
}


##
# a helper class to permit secure loading of untrusted yaml documents
package conditional;
sub yaml_load {
	my ($class, $node) = @_;
	unless (ref($node) eq 'SCALAR') {
		die '!conditional tag must be used with a scalar';
	}
	return bless $node if ref($node) eq 'SCALAR';
}
