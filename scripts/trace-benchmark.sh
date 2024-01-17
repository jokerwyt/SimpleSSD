#!/usr/bin/bash



mkdir -p tracebench_result
for map in page fast
do 
    # trace: case, exchange, homes, ikki
    for trace in case exchange homes ikki
    do
        for fill in 0.5
        do
            # echo the command
            echo "./simplessd-standalone ./config/trace_$trace.cfg ./simplessd/config/$map\_fill_$fill.cfg . > tracebench_result/$map\_$trace\_fill_$fill.txt"

            ./simplessd-standalone ./config/trace_$trace.cfg ./simplessd/config/$map\_fill_$fill.cfg . > tracebench_result/$map\_$trace\_fill_$fill.txt &
        done
    done
done

# wait all background processes to finish
wait

# Path: scripts/trace-benchmark.sh