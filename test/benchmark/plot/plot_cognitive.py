import argparse
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

parser = argparse.ArgumentParser()

args = parser.parse_args()
print(args)

data = pd.read_csv('cognitive.csv')
print(data)

BAR_WIDTH = 0.25
INTERVAL_WIDTH = 0.02

plt.figure(figsize=(5.0, 3.5))
plt.gca().yaxis.grid(linestyle='dotted', color='#555555')
plt.tight_layout(pad=2.0)

rects_spark = plt.bar(data.index - BAR_WIDTH - INTERVAL_WIDTH, data['spark'], BAR_WIDTH, color='#cccccc', label='Spark')
# rects_blaze = plt.bar(data.index, data['blaze'], BAR_WIDTH, color='#019427', label='Blaze', zorder=3)
# rects_blaze_opt = plt.bar(data.index + BAR_WIDTH + INTERVAL_WIDTH, data['blaze_opt'], BAR_WIDTH, color='#016A1C', label='Blaze OPT', zorder=3)
# rects_blaze = plt.bar(data.index, data['blaze'], BAR_WIDTH, color='#0071b5', label='Blaze', zorder=3)
# rects_blaze_opt = plt.bar(data.index + BAR_WIDTH + INTERVAL_WIDTH, data['blaze_opt'], BAR_WIDTH, color='#2091d5', label='Blaze Opt.', zorder=3)
rects_blaze = plt.bar(data.index, data['blaze'], BAR_WIDTH, color='#fad56d', label='Blaze')
# rects_blaze_opt = plt.bar(data.index + BAR_WIDTH + INTERVAL_WIDTH, data['blaze_opt'], BAR_WIDTH, color='#ef6c00', label='Blaze OPT')

plt.legend()
plt.xticks(data.index - (BAR_WIDTH + INTERVAL_WIDTH) / 2, ('Wordcount', 'Pagerank', 'K-Means', 'EM GMM', 'NN100'))
# plt.xticks(data.index - (BAR_WIDTH + INTERVAL_WIDTH) / 2, ('2', '4', '8', '16'))
plt.title('Cognitive Load (Less is Better)')
# plt.xlabel('Tasks')
plt.ylabel('Distinct APIs Used')
plt.savefig('cognitive.eps', format='eps', dpi=300)
plt.show()
