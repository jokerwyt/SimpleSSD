#!/usr/bin/bash


# ./simplessd-standalone ./config/sample.cfg ./simplessd/config/sample.cfg .
#
# replace ./config/sample.cfg with
# ./config/{seqread,seqwrite,randread,randwrite,readwrite,randrw}.cfg
#  
# replace ./simplessd/config/sample.cfg with
# ./simplessd/config/{page,fast}_fill_{0,0.5}.cfg
#
#
# and redirect the stdout to result/{mapping}_{seqread,seqwrite,randread,randwrite,readwrite,randrw}_fill_{0,0.5}.txt

mkdir -p microbench_result
for map in page fast
do 
    for iostyle in seqread seqwrite randread randwrite readwrite randrw
    do
        for fill in 0.5
        do
            ./simplessd-standalone ./config/$iostyle.cfg ./simplessd/config/$map\_fill_$fill.cfg . > microbench_result/$map\_$iostyle\_fill_$fill.txt &
        done
    done
done

# wait all background processes to finish
wait

# Path: scripts/mirco-benchmark.sh