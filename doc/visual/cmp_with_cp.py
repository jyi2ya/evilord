import matplotlib.pyplot as plt

plt.rcParams['font.sans-serif']=['SimHei']

fig, ax = plt.subplots()

items = ['repair1', 'repair2', 'read', 'write', 'cp']
times = [27, 28, 52, 53, 53]
bar_colors = ['tab:blue', 'tab:blue', 'tab:green', 'tab:green', 'tab:green']

p = ax.bar(items, times, color=bar_colors)

ax.set_ylabel('时间')
ax.set_title('与 coreutils 里面的 cp(1) 耗时对比')
ax.legend(title='repair1\n- 指坏掉一块磁盘时的情况\nrepair2\n- 指坏掉两块磁盘时的情况')
ax.bar_label(p)

plt.savefig("cmp_with_cp.svg")
plt.show()
