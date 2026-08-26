#!/usr/bin/gnuplot -persist

set print "-"
set terminal pdf enhanced font 'Verdana,4'
set out      output
set datafile separator ','

# Set color of the lines
colors = "#4363d8 #e6194b #3cb44b #ffe119 #f58231 #911eb4 #46f0f0 #f032e6 #bcf60c #fabebe #008080 #e6beff \
          #9a6324 #fffac8 #800000 #aaffc3 #808000 #ffd8b1 #000075 #808080 #000000 #ff0000 #00ff00 #0000ff"
i = 1;  do for [color in colors ]{ set linetype i lc rgb color;  i = i + 1;  }

sequences = "Longdress Soldier Basketball Dancer Football Levi Mitch Thomas"
labels = "Grid&{x}D1 Grid&{x}D2 Grid&{x}Luma Grid&{x}Chroma&{x}Cb Grid&{x}Chroma&{x}Cr \
          Ibsm&{x}Geom Ibsm&{x}Luma Enc&{x}time&{x}(sec.) Dec&{x}time&{x}(sec.) Enc&{x}memory&{x}(Mb.) Dec&{x}memory&{x}(Mb.) \
		  EB&{x}Enc&{x}time&{x}(sec.) EB&{x}Dec&{x}time&{x}(sec.) \
		  EB&{x}Total&{x}Enc&{x}time&{x}(ms) EB&{x}Total&{x}Dec&{x}time&{x}(ms) \
		  EB&{x}Enc&{x}convert&{x}time&{x}(ms) EB&{x}Enc&{x}hole&{x}fill&{x}time&{x}(ms) EB&{x}Enc&{x}encode&{x}time&{x}(ms) EB&{x}Enc&{x}AC&{x}coding&{x}time&{x}(ms) EB&{x}Enc&{x}syntax&{x}filling&{x}time&{x}(ms) EB&{x}Enc&{x}syntax&{x}serialize&{x}time&{x}(ms)\
		  EB&{x}Dec&{x}Syntax&{x}AC&{x}time&{x}(ms) EB&{x}Dec&{x}decode&{x}time&{x}(ms) EB&{x}Dec&{x}post&{x}reindex&{x}time&{x}(ms) EB&{x}Dec&{x}convert&{x}time&{x}(ms) \
		  EB&{x}FaceID&{x}bytes EB&{x}Position&{x}bytes EB&{x}UVCoords&{x}bytes EB&{x}UVOrient&{x}bytes EB&{x}UVSeams&{x}bytes EB&{x}CLERS&{x}bytes EB&{x}Handle&{x}bytes EB&{x}Deduplication&{x}bytes EB&{x}Total&{x}bytes"

# init graph parameters
unset title
set ylabel
set border
set xtics
set ytics
set border 4095
set grid xtics mytics
set grid ytics mytics
set grid
unset key

print "Create: ", output

#################################
# Draw per condition 1,2 results

do for [cond=1:2] {
  do for [seq=1:8] {

	# init graph parameters
	unset title
	set ylabel
	set border
	set xtics
	set ytics
	set border 4095
	set grid xtics mytics
	set grid ytics mytics
	set grid
	unset key
  
	print "Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq) . " - Frame = ". frame
  	
	set multiplot layout 2,2 title "Detailed runtime profiling: Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq) . " - Frame = ". frame
	
	# draw detailed profiling histograms for runtime
	unset yrange
	unset xrange
	set border 4095
	set tics
	set xlabel
	set ylabel
	unset key
	set style data histogram
	set bars  fullwidth # width of error bars ends
	set style histogram errorbars gap 1 lw 0.5 
	set style fill solid noborder
	set yrange [0:*]
	do for [index=24:27:3 ] {
		set ylabel word( labels, 14 + (index - 24 ) / 3 )
		plot for [ name in csvFiles ] \
			name \
			every::(1+8+(seq-1)*5+(cond-1)*40)::(5+8+(seq-1)*5+(cond-1)*40) \
			using index+1:index:index+2 with histogram \
			title name
	}

	# draw labels
	set key center center
	set border 0
	unset tics
	unset xlabel
	unset ylabel
	set yrange [0:1]
	plot for [ name in names ] 2 t name
  	
	set multiplot layout 2,6 title "Detailed runtime profiling: Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq) . " - Frame = ". frame
	
	# draw detailed profiling histograms for runtime
	unset yrange
	unset xrange
	set border 4095
	set tics
	set xlabel
	set ylabel
	unset key
	set style data histogram
	set style histogram errorbars gap 1 
	set style fill solid noborder
	set boxwidth 1 relative
	set yrange [0:*]
	do for [index=30:57:3 ] {
		set ylabel word( labels, 16 + (index - 30 ) / 3 )
		plot for [ name in csvFiles ] \
			name \
			every::(1+8+(seq-1)*5+(cond-1)*40)::(5+8+(seq-1)*5+(cond-1)*40) \
			using index+1:index:index+2 with histogram \
			title name
	}
	
	# draw labels
	set key center center
	set border 0
	unset tics
	unset xlabel
	unset ylabel
	set yrange [0:1]
	plot for [ name in names ] 2 t name
	
	set multiplot layout 2,5 title "Detailed payload profiling: Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq) . " - Frame = ". frame
	
	# draw detailed profiling histograms for payload
	unset yrange
	unset xrange
	set border 4095
	set tics
	set xlabel
	set ylabel
	unset key
	set style data histogram
	set style histogram cluster gap 1 
	set style fill solid noborder
	set boxwidth 1 relative
	set yrange [0:*]
	do for [index=60:68:1 ] {
		set ylabel word( labels, 26 + index - 60 )
		plot for [ name in csvFiles ] \
			name \
			every::(1+8+(seq-1)*5+(cond-1)*40)::(5+8+(seq-1)*5+(cond-1)*40) \
			using index with histogram \
			title name
	}
	
	# draw labels
	set key center center
	set border 0
	unset tics
	unset xlabel
	unset ylabel
	set yrange [0:1]
	plot for [ name in names ] 2 t name
  }
}

#################################
# Draw per condition 0 results

set multiplot layout 2,2 title "Runtime profiling Cond = 0 - Sequence = all - Frame = ". frame

# draw detailed profiling histograms for runtime
unset yrange
unset xrange
set border 4095
set tics
set xlabel
set ylabel
unset key
set style data histogram
set bars  fullwidth # width of error bars ends
set style histogram errorbars gap 1 lw 1 
set style fill solid noborder
set yrange [0:*]
do for [index=24:27:3 ] {
	set ylabel word( labels, 14 + (index - 24 ) / 3 )
	plot for [ name in csvFiles ] \
		name \
		every::1::8 \
		using index+1:index:index+2 with histogram \
		title name
}

# draw labels
set key center center
set border 0
unset tics
unset xlabel
unset ylabel
set yrange [0:1]
plot for [ name in names ] 2 t name

set multiplot layout 2,6 title "Detailed runtime profiling Cond = 0 - Sequence = all - Frame = ". frame

# draw detailed profiling histograms for runtime
unset yrange
unset xrange
set border 4095
set tics
set xlabel
set ylabel
unset key
set style data histogram
set bars  fullwidth # width of error bars ends
set style histogram errorbars gap 1 lw 1 
set style fill solid noborder
set yrange [0:*]
do for [index=30:57:3 ] {
	set ylabel word( labels, 16 + (index - 30 ) / 3 )
	plot for [ name in csvFiles ] \
		name \
		every::1::8 \
		using index+1:index:index+2 with histogram \
		title name
}

# draw labels
set key center center
set border 0
unset tics
unset xlabel
unset ylabel
set yrange [0:1]
plot for [ name in names ] 2 t name

set multiplot layout 2,5 title "Detailed payload profiling Cond = 0 - Sequence = all - Frame = ". frame

# draw detailed profiling histograms for payload
unset yrange
unset xrange
set border 4095
set tics
set xlabel
set ylabel
unset key
set style data histogram
set style histogram cluster gap 1 
set style fill solid noborder
set yrange [0:*]
do for [index=60:68:1 ] {
	set ylabel word( labels, 26 + index - 60 )
	plot for [ name in csvFiles ] \
		name \
		every::1::8 \
		using index with histogram \
		title name
}

# draw labels
set key center center
set border 0
unset tics
unset xlabel
unset ylabel
set yrange [0:1]
plot for [ name in names ] 2 t name
