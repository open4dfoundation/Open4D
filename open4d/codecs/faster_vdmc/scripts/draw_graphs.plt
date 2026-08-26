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
          Ibsm&{x}Geom Ibsm&{x}Luma Enc&{x}time&{x}(sec.) Dec&{x}time&{x}(sec.) Enc&{x}memory&{x}(Mb.) Dec&{x}memory&{x}(Mb.)"

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
do for [cond=1:2] {
  do for [seq=1:8] {
    print "Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq) . " - Frame = ". frame
    set multiplot layout 3,4 title "Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq) . " - Frame = ". frame

    # draw bitrate/psnr graphs
    set xrange [*:*]
    do for [index in '11 12 16 17 13 14 15' ] {
      colnum = index + 0
      set ylabel word( labels, int( colnum ) - 10 )
      set yrange [*:*]
      do for [ name in csvFiles ] {
        stats name using colnum index 0 every ::(1+8+(seq-1)*5+(cond-1)*40)::(5+8+(seq-1)*5+(cond-1)*40) prefix "PSNR" nooutput
        if ( PSNR_max > 100 ) { set yrange [*:100]; }
      }
      plot for [ name in csvFiles ] \
        name \
        every::(1+8+(seq-1)*5+(cond-1)*40)::(5+8+(seq-1)*5+(cond-1)*40) \
        using ($5/1000000):(column(colnum+0)) with linespoints pointtype 7 pointsize 0.2
    }

    # draw labels
    set key center center
    set border 0
    unset tics
    unset xlabel
    unset ylabel
    set yrange [0:1]
    plot for [ name in names ] 2 t name

    # draw time and memory histograms
    unset yrange
    unset xrange
    set border 4095
    set tics
    set xlabel
    set ylabel
    unset key
    set style data histogram
    set style histogram cluster gap 1
    set style fill solid 5 border -1
    set boxwidth 0.5
    set yrange [0:*]
    do for [index in '18 19 20 21' ] {
      colnum = index + 0
      set ylabel word( labels, int( colnum ) - 10 )
      plot for [ name in csvFiles ] \
        name \
        every::(1+8+(seq-1)*5+(cond-1)*40)::(5+8+(seq-1)*5+(cond-1)*40) \
        using colnum with histogram \
        title name
    }
  }
}

