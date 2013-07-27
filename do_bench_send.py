#! /usr/bin/python
import numpy
import sys


if len(sys.argv) != 2:
    print "Usage do_speed <file>"
    sys.exit(1)

tofile = sys.argv[1]


i = 0
speeds = []
for line in open(tofile):
    if i < 4:
        i += 1
        continue
    
    (rate,_) = line.split("M")

    rate = int(rate[2:])
    speeds.append(rate)
    i += 1

percentiles = [0,0.5,1,5,10,20,30,40,50,60,70,80,90,95,99,99.5,99.9, 99.999,100]

for p in percentiles:
    print "%6.3f%%  %6uMB/s" % (p,numpy.percentile(speeds,p))




