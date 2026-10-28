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

serial_dataset  = get_dataset(args.benchmark_name + ".serial.orwell.outputfile")
vanilla_dataset = get_dataset(args.benchmark_name + ".vanilla.orwell.outputfile")
# bcktrck_dataset = get_dataset(args.benchmark_name + ".backtracking.adams.outputfile")
# lowover_dataset = get_dataset(args.benchmark_name + ".low.adams.outputfile")
zerover_dataset = get_dataset(args.benchmark_name + ".slowspawn.orwell.outputfile")

def plot_dataset_runtime_problem_size(problemsize, dataset, data_label, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = get_average(dataset[problemsize][core_set[i]])
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)

def plot_dataset_speedup_problem_size(problemsize, dataset, data_label, serial_dataset, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	speedup = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		speedup[i] = get_average(serial_dataset[problemsize][1])/get_average(dataset[problemsize][core_set[i]])
	plt.plot(cores, speedup, label=data_label, linestyle=linestyle)


def plot_dataset_scalability_problem_size(problemsize, dataset, data_label, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	scalability = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		scalability[i] = get_average(dataset[problemsize][1])/get_average(dataset[problemsize][core_set[i]])
	plt.plot(cores, scalability, label=data_label, linestyle=linestyle)


def plot_serial_dataset_runtime_problem_size(problemsize, dataset, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = (get_average(dataset[problemsize][core_set[i]]) if cores[i] in dataset[problemsize].keys() else get_average(dataset[problemsize][1]))
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)

def plot_ref_dataset_speedup_problem_size(problemsize, dataset, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = core_set[i]
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)

def plot_ref_dataset_scalability_problem_size(problemsize, dataset, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = core_set[i]
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)


def plot_runtime_problem_size(problemsize):
	plot_dataset_runtime_problem_size(problemsize, vanilla_dataset, "OpenCilk", 'dotted')
	plot_serial_dataset_runtime_problem_size(problemsize, serial_dataset, "serial", vanilla_dataset, 'solid')
	# plot_dataset_runtime_problem_size(problemsize, bcktrck_dataset, "bcktrck")
	# plot_dataset_runtime_problem_size(problemsize, lowover_dataset, "low overhead")
	plot_dataset_runtime_problem_size(problemsize, zerover_dataset, "backtracking", 'dashed')

def plot_speedup_problem_size(problemsize):
	plot_dataset_speedup_problem_size(problemsize, vanilla_dataset, "OpenCilk", serial_dataset, 'dotted')
	plot_ref_dataset_speedup_problem_size(problemsize, serial_dataset, "Ideal", vanilla_dataset, 'solid')
	# plot_dataset_runtime_problem_size(problemsize, bcktrck_dataset, "bcktrck")
	# plot_dataset_speedup_problem_size(problemsize, lowover_dataset, "low overhead", serial_dataset)
	plot_dataset_speedup_problem_size(problemsize, zerover_dataset, "Backtracking", serial_dataset, 'dashed')


def plot_scalability_problem_size(problemsize):
	plot_dataset_scalability_problem_size(problemsize, vanilla_dataset, "OpenCilk", 'dotted')
	plot_ref_dataset_scalability_problem_size(problemsize, serial_dataset, "Ideal", vanilla_dataset, 'solid')
	# plot_dataset_runtime_problem_size(problemsize, bcktrck_dataset, "bcktrck")
	# plot_dataset_scalability_problem_size(problemsize, lowover_dataset, "low overhead")
	plot_dataset_scalability_problem_size(problemsize, zerover_dataset, "Backtracking", 'dashed')


def plot_all_problemsize_runtimes():
	for problemsize in vanilla_dataset.keys():
		plot_runtime_problem_size(problemsize)
		plt.xlabel('Number of Cores')
		plt.ylabel('Runtime')
		plt.legend(loc='best')
		plt.title(str(args.benchmark_name)  + " size " + str(problemsize))

		plt.savefig("figures/" + args.benchmark_name + "." +str(problemsize) + "_runtime.png")
		plt.clf()

def plot_all_problemsize_speedup():
	for problemsize in vanilla_dataset.keys():
		plot_speedup_problem_size(problemsize)
		plt.xlabel('Number of Cores')
		plt.ylabel('Speedup')
		plt.legend(loc='best')
		plt.title(str(args.benchmark_name)  + " size " + str(problemsize))

		plt.savefig("figures/" + args.benchmark_name + "." +str(problemsize) + "_speedup.png")
		plt.clf()

def plot_all_problemsize_scalability():
	for problemsize in vanilla_dataset.keys():
		plot_scalability_problem_size(problemsize)
		plt.xlabel('Number of Cores')
		plt.ylabel('Scalability')
		plt.legend(loc='best')
		plt.title(str(args.benchmark_name)  + " size " + str(problemsize))

		plt.savefig("figures/" + args.benchmark_name + "." +str(problemsize) + "_scalability.png")
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
plot_all_problemsize_speedup()
plot_all_problemsize_scalability()
print_runtimes_as_csv()