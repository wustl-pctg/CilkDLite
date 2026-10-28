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
basecases = dict();

runtime_names = {
	'Serial' : 'serial',
	'Vanilla' : 'vanilla',
	'Heuristic' : 'unwinding',
	# 'No Heuristic' : 'dev18none',
	# 'Caching' : 'dev18cache',
	# 'Heuristic Backoff' : 'dev18heurbackoff',
	'Stack Walk' : 'stack_walker'
}


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
	try:
		with open(filename, 'r') as csvfile:
			reader = csv.reader(csvfile, delimiter=',')
			for row in reader:
				if len(row) < 3:
					continue
				if row[0] == '' or row[1] == '' or row[2] == '':
					continue
				problemsize = None
				cores = None
				runtime = None
				try:
					problemsize = int(row[1])
					cores = int(row[0])
					runtime = float(row[2])
				except:
					continue
				basecase = 0
				if len(row) > 3:
					basecase = int(row[3])
				basecases[basecase] = 0
				if not problemsize in data:
					data[problemsize] = dict();
				if not cores in data[problemsize]:
					data[problemsize][cores] = dict()
				if not basecase in data[problemsize][cores]:
					data[problemsize][cores][basecase] = list()
				data[problemsize][cores][basecase].append(runtime)
		return data
	except FileNotFoundError:
		return None

datasets = dict()
for runtime in runtime_names.keys():
	datasets[runtime] = get_dataset(args.benchmark_name + f".{runtime_names[runtime]}.orwell.outputfile")


# serial_dataset  = get_dataset(args.benchmark_name + ".serial.orwell.outputfile")
# vanilla_dataset = get_dataset(args.benchmark_name + ".vanilla.orwell.outputfile")
# # bcktrck_dataset = get_dataset(args.benchmark_name + ".backtracking.adams.outputfile")
# # framlss_dataset = get_dataset(args.benchmark_name + ".frameless.adams.outputfile")
# zerover_dataset = get_dataset(args.benchmark_name + ".czero_overhead.orwell.outputfile")
# nohrstc_dataset = get_dataset(args.benchmark_name + ".no_heuristic.orwell.outputfile")
# caching_dataset = get_dataset(args.benchmark_name + ".caching.orwell.outputfile")
# slowspawn_dataset = get_dataset(args.benchmark_name + ".slowspawn.orwell.outputfile")

def plot_dataset_runtime_problem_size(problemsize, dataset, basecase, data_label, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = get_average(dataset[problemsize][core_set[i]][basecase])
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)

def plot_dataset_speedup_problem_size(problemsize, dataset, basecase, data_label, serial_dataset, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	speedup = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		speedup[i] = get_average(serial_dataset[problemsize][1][basecase])/get_average(dataset[problemsize][core_set[i]][basecase])
	plt.plot(cores, speedup, label=data_label, linestyle=linestyle)


def plot_dataset_scalability_problem_size(problemsize, dataset, basecase, data_label, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	scalability = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		scalability[i] = get_average(dataset[problemsize][1][basecase])/get_average(dataset[problemsize][core_set[i]][basecase])
	plt.plot(cores, scalability, label=data_label, linestyle=linestyle)


def plot_serial_dataset_runtime_problem_size(problemsize, dataset, basecase, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = (get_average(dataset[problemsize][core_set[i]]) if cores[i] in dataset[problemsize].keys() else get_average(dataset[problemsize][1]))
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)

def plot_ref_dataset_speedup_problem_size(problemsize, dataset, basecase, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = core_set[i]
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)

def plot_ref_dataset_scalability_problem_size(problemsize, dataset, basecase, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = core_set[i]
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle)


def plot_runtime_problem_size(problemsize, basecase):
	plot_serial_dataset_runtime_problem_size(problemsize, datasets['Serial'], basecase, "Serial", datasets['Vanilla'], 'solid')
	for dataset_label in datasets.keys():
		if not basecase in datasets[dataset_label][problemsize][1]:
			continue
		if dataset_label == 'Serial':
			continue
		if datasets[dataset_label] == None:
			continue
		plot_dataset_runtime_problem_size(problemsize, datasets[dataset_label], basecase, dataset_label, 'dashed')
	# plot_dataset_runtime_problem_size(problemsize, nohrstc_dataset, "No Heuristic", 'dashed')
	# plot_dataset_runtime_problem_size(problemsize, zerover_dataset, "Backtracking", 'dashed')
	# plot_dataset_runtime_problem_size(problemsize, caching_dataset, "Caching", 'dashed')

def plot_speedup_problem_size(problemsize, basecase):
	plot_ref_dataset_speedup_problem_size(problemsize, datasets['Serial'], basecase, "Ideal", datasets['Vanilla'], 'solid')
	for dataset_label in datasets.keys():
		if not basecase in datasets[dataset_label][problemsize][1]:
			continue
		if dataset_label == 'Serial':
			continue
		if datasets[dataset_label] == None:
			continue
		plot_dataset_speedup_problem_size(problemsize, datasets[dataset_label], basecase, dataset_label, datasets['Serial'], 'dashed')
	# plot_dataset_speedup_problem_size(problemsize, nohrstc_dataset, "No Heuristic", serial_dataset, 'dashed')
	# plot_dataset_speedup_problem_size(problemsize, zerover_dataset, "Backtracking", serial_dataset, 'dashed')
	# plot_dataset_speedup_problem_size(problemsize, caching_dataset, "Caching", serial_dataset, 'dashed')

def plot_scalability_problem_size(problemsize, basecase):
	plot_ref_dataset_scalability_problem_size(problemsize, datasets['Serial'], basecase, "Ideal", datasets['Vanilla'], 'solid')
	for dataset_label in datasets.keys():
		if not basecase in datasets[dataset_label][problemsize][1]:
			continue
		if dataset_label == 'Serial':
			continue
		if datasets[dataset_label] == None:
			continue
		plot_dataset_scalability_problem_size(problemsize, datasets[dataset_label], basecase, dataset_label, 'dashed')

def plot_basecase_problem_size(problemsize, dataset, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	for basecase in dataset[problemsize][core_set[0]].keys():
		cores = np.empty(len(core_set))
		runtimes = np.empty(len(core_set))
		for i in range(len(core_set)):
			cores[i] = core_set[i]
			runtimes[i] = get_average(dataset[problemsize][core_set[i]][basecase])
		plt.plot(cores, runtimes, label=("base case " + str(basecase)), linestyle=linestyle)

def plot_all_basecases():
	for problemsize in datasets['Vanilla'].keys():
		for dataset_label in datasets.keys():
			if dataset_label == 'Serial':
				continue
			if datasets[dataset_label] == None:
				continue			
			plot_basecase_problem_size(problemsize, datasets[dataset_label])
			plt.xlabel('Number of Cores')
			plt.ylabel('Runtime')
			plt.legend(loc='best')
			plt.title(str(dataset_label) + " " + str(args.benchmark_name)  + " size " + str(problemsize))

			plt.savefig("figures/" + args.benchmark_name + "." + str(problemsize) + "." + str(dataset_label) +  "_runtime.png")
			plt.clf()

def plot_basecase_scalability_problem_size(problemsize, dataset, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	for basecase in dataset[problemsize][core_set[0]].keys():
		cores = np.empty(len(core_set))
		runtimes = np.empty(len(core_set))
		for i in range(len(core_set)):
			cores[i] = core_set[i]
			runtimes[i] = get_average(dataset[problemsize][1][basecase])/get_average(dataset[problemsize][core_set[i]][basecase])
		plt.plot(cores, runtimes, label=("base case " + str(basecase)), linestyle=linestyle)

def plot_all_basecases_scalability():
	for problemsize in datasets['Vanilla'].keys():
		for dataset_label in datasets.keys():
			if dataset_label == 'Serial':
				continue
			if datasets[dataset_label] == None:
				continue			
			plot_basecase_scalability_problem_size(problemsize, datasets[dataset_label])
			plt.xlabel('Number of Cores')
			plt.ylabel('Scalability')
			plt.legend(loc='best')
			plt.title(str(dataset_label) + " " + str(args.benchmark_name)  + " size " + str(problemsize))

			plt.savefig("figures/" + args.benchmark_name + "." + str(problemsize) + "." + str(dataset_label) +  "_scalability.png")
			plt.clf()

def plot_all_problemsize_runtimes():
	for problemsize in datasets['Vanilla'].keys():
		for basecase in basecases.keys():
			plot_runtime_problem_size(problemsize, basecase)
			plt.xlabel('Number of Cores')
			plt.ylabel('Runtime')
			plt.legend(loc='best')
			plt.title(str(args.benchmark_name)  + " size " + str(problemsize) + ((" basecase " + str(basecase)) if int(basecase) > 0 else ""))

			plt.savefig("figures/" + args.benchmark_name + "." + str(problemsize) + "." + str(basecase) +  "_runtime.png")
			plt.clf()

def plot_all_problemsize_speedup():
	for problemsize in datasets['Vanilla'].keys():
		for basecase in basecases.keys():
			plot_speedup_problem_size(problemsize, basecase)
			plt.xlabel('Number of Cores')
			plt.ylabel('Speedup')
			plt.legend(loc='best')
			plt.title(str(args.benchmark_name)  + " size " + str(problemsize) + ((" basecase " + str(basecase)) if int(basecase) > 0 else ""))

			plt.savefig("figures/" + args.benchmark_name + "." + str(problemsize) + "." + str(basecase) + "_speedup.png")
			plt.clf()

def plot_all_problemsize_scalability():
	for problemsize in datasets['Vanilla'].keys():
		for basecase in basecases.keys():
			plot_scalability_problem_size(problemsize, basecase)
			plt.xlabel('Number of Cores')
			plt.ylabel('Scalability')
			plt.legend(loc='best')
			plt.title(str(args.benchmark_name)  + " size " + str(problemsize) + ((" basecase " + str(basecase)) if int(basecase) > 0 else ""))

			plt.savefig("figures/" + args.benchmark_name + "." + str(problemsize) + "." + str(basecase) + "_scalability.png")
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
		print_dataset_as_csv("No Heuristic", nohrstc_dataset, problemsize, vanilla_dataset, serial_dataset)

plot_all_basecases()
plot_all_basecases_scalability()
# plot_all_problemsize_runtimes()
# plot_all_problemsize_speedup()
# plot_all_problemsize_scalability()
# print_runtimes_as_csv()