import sys,csv, math
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.pyplot import cm
from cycler import cycler
import argparse
parser = argparse.ArgumentParser(description='Takes benchmark to parse')
parser.add_argument('benchmark_name', 
                    help='The name of the benchmark to parse')
args = parser.parse_args();

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

def get_dataset(filename):
	data = dict()
	with open(filename, 'r') as csvfile:
		reader = csv.reader(csvfile, delimiter=',')
		for row in reader:
			problemsize = int(row[1])
			cores = int(row[0])
			runtime = float(row[2])
			if not problemsize in data:
				data[problemsize] = dict();
			if not cores in data[problemsize]:
				data[problemsize][cores] = list()
			data[problemsize][cores].append(runtime)
	return data

serial_dataset  = get_dataset(args.benchmark_name + ".serial.adams.outputfile")
vanilla_dataset = get_dataset(args.benchmark_name + ".vanilla.adams.outputfile")
# bcktrck_dataset = get_dataset(args.benchmark_name + ".backtracking.adams.outputfile")
# framlss_dataset = get_dataset(args.benchmark_name + ".frameless.adams.outputfile")
zerover_dataset = get_dataset(args.benchmark_name + ".czero_overhead.adams.outputfile")

def plot_dataset_runtime_problem_size(problemsize, dataset, data_label):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = get_average(dataset[problemsize][core_set[i]])
	plt.plot(cores, runtimes, label=data_label)

def plot_runtime_problem_size(problemsize):
	plot_dataset_runtime_problem_size(problemsize, vanilla_dataset, "vanilla")
	# plot_dataset_runtime_problem_size(problemsize, bcktrck_dataset, "bcktrck")
	# plot_dataset_runtime_problem_size(problemsize, framlss_dataset, "framlss")
	plot_dataset_runtime_problem_size(problemsize, zerover_dataset, "zero_overhead")

def plot_all_problemsize_runtimes():
	for problemsize in vanilla_dataset.keys():
		plot_runtime_problem_size(problemsize)
		plt.xlabel('Number of Cores')
		plt.ylabel('Runtime')
		plt.legend(loc='best')
		plt.title(str(args.benchmark_name)  + " size " + str(problemsize))

		plt.savefig("figures/" + args.benchmark_name + "." +str(problemsize) + "_runtime.png")
		plt.clf()

def print_serial_as_csv(dataset, problemsize):
	print("Serial")
	runtime_str = "Runtimes"
	for core in dataset[problemsize].keys():
		runtime_str += "," + str(get_average(dataset[problemsize][core]))
	print(runtime_str)
	stddev_str = "Std. Dev"
	for core in dataset[problemsize].keys():
		stddev_str += "," + str(get_stddev_pct(dataset[problemsize][core]))
	print(stddev_str)

def print_vanilla_as_csv(dataset, problemsize, serial):
	print("Vanilla")
	runtime_str = "Runtimes"
	for core in dataset[problemsize].keys():
		runtime_str += "," + str(get_average(dataset[problemsize][core]))
	print(runtime_str)
	stddev_str = "Std. Dev"
	for core in dataset[problemsize].keys():
		stddev_str += "," + str(get_stddev_pct(dataset[problemsize][core]))
	print(stddev_str)
	scalability_str = "Scalability"
	for core in dataset[problemsize].keys():
		scalability_str += "," + str(float(get_average(dataset[problemsize][1]))/float(get_average(dataset[problemsize][core])))
	print(scalability_str)
	speedup_str = "Speedup"
	for core in dataset[problemsize].keys():
		speedup_str += "," + str(float(get_average(serial[problemsize][1]))/float(get_average(dataset[problemsize][core])))
	print(speedup_str)

def print_dataset_as_csv(label, dataset, problemsize, vanilla, serial):
	print(label)
	runtime_str = "Runtimes"
	for core in dataset[problemsize].keys():
		runtime_str += "," + str(get_average(dataset[problemsize][core]))
	print(runtime_str)
	stddev_str = "Std. Dev"
	for core in dataset[problemsize].keys():
		stddev_str += "," + str(get_stddev_pct(dataset[problemsize][core]))
	print(stddev_str)
	scalability_str = "Scalability"
	for core in dataset[problemsize].keys():
		scalability_str += "," + str(float(get_average(dataset[problemsize][1]))/float(get_average(dataset[problemsize][core])))
	print(scalability_str)
	speedup_str = "Speedup"
	for core in dataset[problemsize].keys():
		speedup_str += "," + str(float(get_average(serial[problemsize][1]))/float(get_average(dataset[problemsize][core])))
	print(speedup_str)
	vanilla_comparison_str = "Vanilla vs."
	for core in dataset[problemsize].keys():
		vanilla_comparison_str += "," + str(float(get_average(vanilla[problemsize][core]))/float(get_average(dataset[problemsize][core])))
	print(vanilla_comparison_str)


def print_runtimes_as_csv():
	for problemsize in vanilla_dataset.keys():
		print("\nProblem Size: " + str(problemsize))
		print_serial_as_csv(serial_dataset, problemsize)
		print_vanilla_as_csv(vanilla_dataset, problemsize, serial_dataset)
		# print_dataset_as_csv("Backtracking", bcktrck_dataset, problemsize, vanilla_dataset, serial_dataset)
		# print_dataset_as_csv("Frameless", framlss_dataset, problemsize, vanilla_dataset, serial_dataset)
		print_dataset_as_csv("Zero Overhead", zerover_dataset, problemsize, vanilla_dataset, serial_dataset)


plot_all_problemsize_runtimes()
print_runtimes_as_csv()