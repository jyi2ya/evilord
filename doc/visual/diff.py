import matplotlib.pyplot as plt
import numpy as np

plt.rcParams['font.sans-serif']=['SimHei']

labels = ['write', 'read', 'repair1', 'repair2']
after = [71.5, 53.5, 35.1, 44.2]
before = [76.1, 72.9, 45.5, 47.6]

x = np.arange(len(labels))  # the label locations
width = 0.35  # the width of the bars

fig, ax = plt.subplots()
rects1 = ax.bar(x - width/2, after, width, label='优化')
rects2 = ax.bar(x + width/2, before, width, label='朴素')

# Add some text for labels, title and custom x-axis tick labels, etc.
ax.set_ylabel('时间')
ax.set_title("优化后的实现与朴素实现耗时对比", fontsize = 13)
ax.set_xticks(x, labels)
ax.legend(title='repair1 - 指坏掉一块磁盘时的情况\nrepair2 - 指坏掉两块磁盘时的情况')

ax.bar_label(rects1, padding=3)
ax.bar_label(rects2, padding=3)

fig.tight_layout()

plt.savefig("diff.svg")
plt.show()
