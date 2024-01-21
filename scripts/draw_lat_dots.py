# mse data in latency.csv to draw plot graph
# 0, 0, 4096, 516813886
# 1, 4096, 4096, 516813886
# 2, 8192, 4096, 516813886
# 3, 12288, 4096, 516813886
# 4, 16384, 4096, 516813886
# 5, 20480, 4096, 516813886
# 6, 24576, 4096, 516813886
# 7, 28672, 4096, 516813886
# ...
#
# every line is an operation. We only need the last number which is latency in ps.
# the x-axis is is time, the y-axis is latency.
# draw dots in red color, do not draw lines.

import matplotlib.pyplot as plt
import numpy as np
import sys
import os

def draw(filename, picpath):

    # prune the file first.
    # only keep line with format like:
    # 16, 15681228800, 4096, 1315413886
    
    # do not include 
    # Latency (ns): min=515413.886000, max=1315413.886000, avg=918392.401625, stdev=508835.820316


    tmp_filepath = '/tmp/wyt-tmp'

    with open(filename, 'r') as f:
        lines = f.readlines()
        with open(tmp_filepath, 'w') as tmp:
            for line in lines:
                if line.count(',') == 3 and line.count('Latency') == 0:
                    tmp.write(line)

    data = np.loadtxt(tmp_filepath, delimiter=',', usecols=(3), unpack=True)

    data = data / 1000 / 1000 / 1000

    # if figure open, close it
    plt.close()


    # make it bigger
    plt.rcParams["figure.figsize"] = (10, 5)

    # create a new figure
    plt.figure()


    # draw plot
    plt.plot(data, 'ro', markersize=1)
    plt.ylabel('Latency (ms)')
    plt.xlabel('Request')

    # fix y range 0-1500
    # plt.ylim(0, 1500)

    # set x-axis limits
    plt.xlim(0, len(data))  # Increase the x-axis limit

    # put avg latency on the graph
    # draw a different color line
    avg = np.average(data)
    # plt.axhline(y=avg, color='g', linestyle='-')
    # create a separate text box to display additional notes
    text_box = plt.text(0.90, 0.90, f'avg: {avg:.3f}ms\nmin: {np.min(data):.3f}ms\nmax: {np.max(data):.3f}ms',
                        transform=plt.gca().transAxes, fontsize=12, verticalalignment='top',
                        bbox=dict(facecolor='white', edgecolor='black', boxstyle='round,pad=0.5'))
    
    # adjmst plot margins to make room for the text box

    # put the file name on the graph
    plt.title(filename)
    plt.subplots_adjust(right=0.85)

    # # add text, only 3 digits after decimal point
    # plt.annotate('avg: %.3fms' % avg, xy=(len(data), avg), xytext=(-10, 10),
    #              textcoords='offset points', color='b', fontsize=12, ha='right', zorder=10)

    # # put some additional notes on the space
    # plt.annotate('min: %.3fms' % np.min(data), xy=(len(data), 0), xytext=(-10, -20),
    #              textcoords='offset points', color='b', fontsize=12, ha='right', zorder=10)
    # plt.annotate('max: %.3fms' % np.max(data), xy=(len(data), np.max(data)), xytext=(-10, 10),
    #              textcoords='offset points', color='b', fontsize=12, ha='right', zorder=10)
    
    # remove the original file if exists
    if os.path.exists(picpath):
        os.remove(picpath)

    plt.savefig(picpath)

if __name__ == '__main__':

    # mkdir first

    for test_name in ['microbench', 'tracebench']:
        if not os.path.exists(test_name + '_result/pic'):
            os.mkdir(test_name + '_result/pic')

        for file in os.listdir(test_name + '_result/'):
            print(file)
            # if is not directory
            if os.path.isfile(test_name + '_result/' + file):
                draw(test_name + '_result/' + file, test_name + '_result/pic/' + file + '.png')
