import sys,csv, math
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.pyplot import cm
from cycler import cycler

def get_average(values):
	summation = 0.0
	for value in values:
		summation += value
	return summation/len(values)

def get_stddev(values):
	average = get_average(values)
	summation = 0.0
	for value in values:
		summation += (value - average)**2
	summation = summation/len(values)
	return math.sqrt(summation)

def get_stddev_pct(values):
	average = get_average(values)
	stddev = get_stddev(values)
	return 100*(stddev/average)

def parse_execution_time(filename):
	data = dict()
	with open(filename, 'r') as csvfile:
		reader = csv.reader(csvfile, delimiter=',')
		is_active = False
		labels = list()
		workers = list()
		for row in reader:
			if is_active:
				if len(row) < 2:
					is_active = False
				else:
					if len(labels) < 2:
						for label in row:
							labels.append(label)
						data["labels"] = labels
					else:
						worker = int(row[0])
						if not worker in data:
							data[worker] = dict();
							workers.append(worker)
						for i in range(len(row)):
							data[worker][labels[i]] = row[i]
			else:
				if len(row) > 0 and row[0] == "CSV_STATS_BELOW":
					is_active = True
		data["workers"] = workers
	return data

def parse_backtrack_time(filename):
	data = dict()
	with open(filename, 'r') as csvfile:
		reader = csv.reader(csvfile, delimiter=',')
		is_active = False
		labels = list()
		workers = list()
		data['raw'] = dict()
		for row in reader:
			if is_active:
				if len(row) < 2:
					is_active = False
				else:
					if len(labels) < 2:
						for label in row:
							labels.append(label)
						data["labels"] = labels
					else:
						worker = int(row[0])
						if not worker in data:
							data[worker] = dict();
							workers.append(worker)
						for i in range(len(row)):
							if not labels[i] in data[worker]:
								data[worker][labels[i]] = list()
							if not labels[i] in data['raw']:
								data['raw'][labels[i]] = list()
							data[worker][labels[i]].append(row[i])
							data['raw'][labels[i]].append(row[i])
			else:
				if len(row) > 0 and row[0] == "backtrack_values":
					is_active = True
		data["workers"] = workers
	return data

execution_time_dataset = parse_execution_time("cilksort.backtracking.autopsy.adams.outputfile")
backtrack_time_dataset = parse_backtrack_time("cilksort.backtracking.autopsy.adams.outputfile")


def get_most_backtracks(dataset):
	max_val = 0
	for worker in dataset["workers"]:
		val = len(dataset[worker][dataset["labels"][0]])
		if val > max_val:
			max_val = val
	return max_val

def get_total_backtracks(dataset):
	total = 0
	for worker in dataset["workers"]:
		total += len(dataset[worker][dataset["labels"][0]])
	return total

def plot_backtrack_vs_order(autopsy_name):
	dataset = parse_backtrack_time(str(autopsy_name) + ".adams.outputfile")
	values = list()
	for i in range(get_most_backtracks(dataset)):
		values.append(list())
		for worker in dataset["workers"]:
			if i < len(dataset[worker][dataset["labels"][0]]):
				values[i].append(float(dataset[worker]["time"][i]))
	average_times = np.empty(get_total_backtracks(dataset))
	order = np.empty(get_total_backtracks(dataset))
	j = 0
	for i in range(len(values)):
		print("Order " + str(i) + " has " + str(len(values[i])) + " values")
		for value in values[i]:
			order[j] = i
			average_times[j] = value
			j += 1
	plt.scatter(order, average_times, s=2)
	plt.xlabel('Backtrack Number')
	plt.ylabel('Runtime')
	#plt.yscale("log")
	plt.ylim(0,0.001)
	plt.savefig("figures/" + str(autopsy_name) + "Autopsy_Time_vs_Order.svg")
	plt.clf()

def plot_backtrack_vs_cframes(autopsy_name):
	dataset = parse_backtrack_time(str(autopsy_name) + ".adams.outputfile")
	values = dict()
	for i in range(len(dataset['raw'][dataset['labels'][0]])):
		if int(dataset['raw']['cilk_frames'][i]) not in values:
			values[int(dataset['raw']['cilk_frames'][i])] = list()
		values[int(dataset['raw']['cilk_frames'][i])].append(float(dataset['raw']['time'][i]))
	raw_cilk_frames = list()
	for frame in values.keys():
		raw_cilk_frames.append(frame)
	raw_cilk_frames.sort()
	cilk_frames = np.empty(len(raw_cilk_frames))
	average_times = np.empty(len(raw_cilk_frames))
	for i in range(len(raw_cilk_frames)):
		cilk_frames[i] = raw_cilk_frames[i]
		average_times[i] = get_average(values[raw_cilk_frames[i]])
	plt.plot(cilk_frames, average_times)
	plt.xlabel('Cilk Frames')
	plt.ylabel('Runtime')
	plt.savefig("figures/" + str(autopsy_name) + "Autopsy_Time_vs_CFrames.png")
	plt.clf()

def plot_backtrack_vs_sframes(autopsy_name):
	dataset = parse_backtrack_time(str(autopsy_name) + ".adams.outputfile")
	values = dict()
	for i in range(len(dataset['raw'][dataset['labels'][0]])):
		if int(dataset['raw']['stack_frames'][i]) not in values:
			values[int(dataset['raw']['stack_frames'][i])] = list()
		values[int(dataset['raw']['stack_frames'][i])].append(float(dataset['raw']['time'][i]))
	raw_stack_frames = list()
	for frame in values.keys():
		raw_stack_frames.append(frame)
	raw_stack_frames.sort()
	stack_frames = np.empty(len(raw_stack_frames))
	average_times = np.empty(len(raw_stack_frames))
	for i in range(len(raw_stack_frames)):
		stack_frames[i] = raw_stack_frames[i]
		average_times[i] = get_average(values[raw_stack_frames[i]])
	plt.plot(stack_frames, average_times)
	plt.xlabel('Stack Frames')
	plt.ylabel('Runtime')
	plt.savefig("figures/" + str(autopsy_name) + "Autopsy_Time_vs_SFrames.png")
	plt.clf()

def plot_cframes_vs_order(autopsy_name):
	dataset = parse_backtrack_time(str(autopsy_name) + ".adams.outputfile")
	values = list()
	for i in range(get_most_backtracks(dataset)):
		values.append(list())
		for worker in dataset["workers"]:
			if i < len(dataset[worker][dataset["labels"][0]]):
				values[i].append(int(dataset[worker]["cilk_frames"][i]))
	average_times = np.empty(len(values))
	order = np.empty(len(values))
	for i in range(len(values)):
		order[i] = i
		average_times[i] = get_average(values[i])
	plt.plot(order, average_times)
	plt.xlabel('Avg. Cilk Frames')
	plt.ylabel('Runtime')
	plt.savefig("figures/" + str(autopsy_name) + "Autopsy_Cframes_vs_Order.png")
	plt.clf()

def plot_all_backtracks(autopsy_name):
	plot_backtrack_vs_order(autopsy_name)
	plot_backtrack_vs_cframes(autopsy_name)
	plot_backtrack_vs_sframes(autopsy_name)
	plot_cframes_vs_order(autopsy_name)

plot_all_backtracks("cilksort.backtracking.autopsy")