#!/usr/bin/gnuplot -persist

set print "-"
print "experiments = ", experiments
print "names       = ", names
print "output      = ", output

set terminal pdf enhanced font 'Verdana,4'
set out      output
set datafile separator ','

sequences = "Longdress Soldier Basketball Dancer Mitch Thomas Football Levi"
sequencesShort = "long sold bask danc mitc thom foot levi"

# init graph parameters
numTests=words( experiments )
print "numTests = ", numTests

set size ratio -1
unset key
unset border
unset tics

do for [cond=0:2] {
  do for [seq=1:8] {
    if ( cond == 0 ) { ra=0; rb=0; } else { ra=1; rb=5; }
    do for [rate=ra:rb] {
      unset xlabel
      unset ylabel
      w=320
      h=400
      if ( seq == 1 ){ x=320; y=1300; }
      if ( seq == 2 ){ x=320; y=1300; }
      if ( seq == 3 ){ x=490; y=1130; }
      if ( seq == 4 ){ x=268; y=1100; }
      if ( seq == 5 ){ x=450; y=1220; }
      if ( seq == 6 ){ x=320; y=1420; }
      if ( seq == 7 ){ x=340; y=900;  }
      if ( seq == 8 ){ x=300; y=1130; }
      set xrange [x:x+w]
      set yrange [y:y+h]
      if ( cond == 0 ){
        titlename="Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq ) . " - Frame = ". frame;
        testname='s'.seq.'c'.cond.'_'. word( sequencesShort, seq );
      } else{
        titlename="Cond = ".cond." - Sequence = ".seq. " - ". word( sequences, seq ) . " - Rate = ".rate. " - Frame = ". frame;
        testname='s'.seq.'c'.cond.'r'.rate.'_'. word( sequencesShort, seq );
      }
      print titlename;
      set multiplot layout 2,numTests+1 title titlename
      unset xlabel
      do for [test=1:numTests] {
        set title word(names, test)
        filename=word(experiments, test).'/'. testname .'/dec_light0.png'
        plot filename binary filetype=png with rgbimage
        if ( test == 1 ){
          set title "Original"
          filename=word(experiments, test).'/'. testname .'/src_light0.png'
          plot filename binary filetype=png with rgbimage
        }
      }
      do for [test=1:numTests] {
        set title word(names, test)
        filename=word(experiments, test).'/'. testname .'/dec_light1.png'
        plot filename binary filetype=png with rgbimage
        if ( test == 1 ){
          set title "Original"
          filename=word(experiments, test).'/'. testname .'/src_light1.png'
          plot filename binary filetype=png with rgbimage
        }
      }
    }
  }
}

print "Create: ", output
