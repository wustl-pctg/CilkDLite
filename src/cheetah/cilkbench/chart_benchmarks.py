import sys,csv, math
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.pyplot import cm
from cycler import cycler
import argparse
import os
from pathlib import Path
import tikzplotlib
import webcolors

parser = argparse.ArgumentParser(description='Takes benchmark to parse')
parser.add_argument('benchmark_name',
                    help='The name of the benchmark to parse')
parser.add_argument("--output-dir", type=Path, default=Path(os.environ.get("BUILD_ROOT", Path(__file__).resolve().parents[3] / "build")) / "results" / "figures")
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
basecases = dict()

color_dict = {
	'Serial' : '#648FFF',
	'Ideal' : '#648FFF',
	'OpenCilk': '#DC267F',
	'Stack Walker' : '#FFB000'
}

colors=['#648FFF', '#DC267F', '#FFB000']

runtime_names = {
	'Serial' : 'serial',
	'OpenCilk' : 'vanilla',
	'Segfault': 'segfault',
	# 'OpenCilk libc' : 'vanilla_libc',
	# 'Unwinding Fast SC' : 'unwinding_fast',
	# 'Stack Walk Heuristic' : 'stack_walker',
	# 'Unwinding' : 'unwinding_raw',
	# 'Unwinding SC Caching' : 'unwinding',
	# 'Stack Walk' : 'stack_walker_sigfree',
	'Stack Walker tcmalloc': 'tcmallocmaster',
	'Stack Walker' : 'stack_walker_nocache',
	# 'Stack Walker Caching' : 'stack_walker',
	# 'Stack Walker Signal Free' : 'stack_walker_nocache_polling',
	# 'Stack Walker Backoff' : 'stack_walker_backoff',
	# 'Unwinding' : 'unwinding',
	# 'Unwinding_nocache' : 'unwinding_caching',
	# 'OpenCilk Libc' : 'vanilla_libc',
	# 'Caching' : 'dev18cache',
	# 'Heuristic Backoff' : 'dev18heurbackoff',
	# 'Stack Walk Inlined' : 'stack_walker_inlined'
}


def get_average(values):
	summation = 0.0
	for value in values:
		summation += value
	return summation/len(values)

def get_average_or_null(datasets, runtime, problemsize, cores, basecase):
	if not datasets:
		return "-"
	if not (runtime in datasets):
		return "-"
	if not (datasets[runtime]):
		return "-"
	if not (problemsize in datasets[runtime]):
		return "-"
	if not cores in datasets[runtime][problemsize]:
		return "-"
	if not basecase in datasets[runtime][problemsize][cores]:
		return "-"
	return get_average(datasets[runtime][problemsize][cores][basecase])

def get_stddev_or_null(datasets, runtime, problemsize, cores, basecase):
	if not datasets:
		return "-"
	if not (runtime in datasets):
		return "-"
	if not (datasets[runtime]):
		return "-"
	if not (problemsize in datasets[runtime]):
		return "-"
	if not cores in datasets[runtime][problemsize]:
		return "-"
	if not basecase in datasets[runtime][problemsize][cores]:
		return "-"
	return get_stddev_pct(datasets[runtime][problemsize][cores][basecase])


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
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle, color=color_dict[data_label])

def plot_dataset_speedup_problem_size(problemsize, dataset, basecase, data_label, serial_dataset, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	speedup = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		speedup[i] = get_average(serial_dataset[problemsize][1][basecase])/get_average(dataset[problemsize][core_set[i]][basecase])
	plt.plot(cores, speedup, label=data_label, linestyle=linestyle, color=color_dict[data_label])


def plot_dataset_scalability_problem_size(problemsize, dataset, basecase, data_label, linestyle='solid'):
	core_set = list(dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	scalability = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		scalability[i] = get_average(dataset[problemsize][1][basecase])/get_average(dataset[problemsize][core_set[i]][basecase])
	plt.plot(cores, scalability, label=data_label, linestyle=linestyle, color=color_dict[data_label])


def plot_serial_dataset_runtime_problem_size(problemsize, dataset, basecase, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = (get_average(dataset[problemsize][core_set[i]][basecase]) if cores[i] in dataset[problemsize].keys() else get_average(dataset[problemsize][1][basecase]))
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle, color=color_dict[data_label])

def plot_ref_dataset_speedup_problem_size(problemsize, dataset, basecase, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = core_set[i]
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle, color=color_dict[data_label])

def plot_ref_dataset_scalability_problem_size(problemsize, dataset, basecase, data_label, vanilla_dataset, linestyle='solid'):
	core_set = list(vanilla_dataset[problemsize].keys())
	cores = np.empty(len(core_set))
	runtimes = np.empty(len(core_set))
	for i in range(len(core_set)):
		cores[i] = core_set[i]
		runtimes[i] = core_set[i]
	plt.plot(cores, runtimes, label=data_label, linestyle=linestyle, color=color_dict[data_label])


linestyles = ['dashed', 'solid']
def plot_runtime_problem_size(problemsize, basecase):
	plot_serial_dataset_runtime_problem_size(problemsize, datasets['Serial'], basecase, "Serial", datasets['OpenCilk'], 'dotted')
	linestyle_index = 0
	for dataset_label in datasets.keys():
		linestyle_index = (linestyle_index + 1) % 2
		if dataset_label == 'Serial':
			continue
		if datasets[dataset_label] == None:
			continue
		if not basecase in datasets[dataset_label][problemsize][1]:
			continue
		plot_dataset_runtime_problem_size(problemsize, datasets[dataset_label], basecase, dataset_label, linestyles[linestyle_index])
	# plot_dataset_runtime_problem_size(problemsize, nohrstc_dataset, "No Heuristic", 'dashed')
	# plot_dataset_runtime_problem_size(problemsize, zerover_dataset, "Backtracking", 'dashed')
	# plot_dataset_runtime_problem_size(problemsize, caching_dataset, "Caching", 'dashed')

def plot_speedup_problem_size(problemsize, basecase):
	plot_ref_dataset_speedup_problem_size(problemsize, datasets['Serial'], basecase, "Ideal", datasets['OpenCilk'], 'dotted')
	linestyle_index = 0
	for dataset_label in datasets.keys():
		linestyle_index = (linestyle_index + 1) % 2
		if dataset_label == 'Serial':
			continue
		if datasets[dataset_label] == None:
			continue
		if not basecase in datasets[dataset_label][problemsize][1]:
			continue
		plot_dataset_speedup_problem_size(problemsize, datasets[dataset_label], basecase, dataset_label, datasets['Serial'], linestyles[linestyle_index])
	# plot_dataset_speedup_problem_size(problemsize, nohrstc_dataset, "No Heuristic", serial_dataset, 'dashed')
	# plot_dataset_speedup_problem_size(problemsize, zerover_dataset, "Backtracking", serial_dataset, 'dashed')
	# plot_dataset_speedup_problem_size(problemsize, caching_dataset, "Caching", serial_dataset, 'dashed')


def plot_scalability_problem_size(problemsize, basecase):
	plot_ref_dataset_scalability_problem_size(problemsize, datasets['Serial'], basecase, "Ideal", datasets['OpenCilk'], 'solid')
	linestyle_index = 0
	for dataset_label in datasets.keys():
		linestyle_index = (linestyle_index + 1) % 2
		if dataset_label == 'Serial':
			continue
		if datasets[dataset_label] == None:
			continue
		if not basecase in datasets[dataset_label][problemsize][1]:
			continue
		plot_dataset_scalability_problem_size(problemsize, datasets[dataset_label], basecase, dataset_label, linestyles[linestyle_index])


def plot_all_problemsize_runtimes():
	for problemsize in datasets['OpenCilk'].keys():
		for basecase in basecases.keys():
			plot_runtime_problem_size(problemsize, basecase)
			plt.xlabel('Number of Cores', fontsize=14)
			plt.ylabel('Runtime', fontsize=14)
			plt.legend(loc='best', fontsize=14)
			plt.xticks(fontsize=12)
			plt.yticks(fontsize=12)
			plt.title(str(args.benchmark_name)  + " size " + str(problemsize) + ((" basecase " + str(basecase)) if int(basecase) > 0 else ""), fontsize=18)

			plt.savefig((str(args.output_dir) + "/") + args.benchmark_name + "-" + str(problemsize) + ("-" + str(basecase) if basecase > 0 else "") + "-runtime.png")
			plt.savefig((str(args.output_dir) + "/") + args.benchmark_name + "-" + str(problemsize) + ("-" + str(basecase) if basecase > 0 else "") + "-runtime.svg")
			plt.clf()

def plot_all_problemsize_speedup():
	for problemsize in datasets['OpenCilk'].keys():
		for basecase in basecases.keys():
			plot_speedup_problem_size(problemsize, basecase)
			plt.xlabel('Number of Cores', fontsize=14)
			plt.ylabel('Speedup', fontsize=14)
			plt.legend(loc='best', fontsize=14)
			plt.xticks(fontsize=12)
			plt.yticks(fontsize=12)
			plt.title(str(args.benchmark_name)  + " size " + str(problemsize) + ((" basecase " + str(basecase)) if int(basecase) > 0 else ""), fontsize=18)
			plt.savefig((str(args.output_dir) + "/") + args.benchmark_name + "-" + str(problemsize) + ("-" + str(basecase) if basecase > 0 else "") + "-speedup.png")
			plt.savefig((str(args.output_dir) + "/") + args.benchmark_name + "-" + str(problemsize) + ("-" + str(basecase) if basecase > 0 else "") + "-speedup.svg")
			plt.clf()

def plot_all_problemsize_scalability():
	for problemsize in datasets['OpenCilk'].keys():
		for basecase in basecases.keys():
			plot_scalability_problem_size(problemsize, basecase)
			plt.xlabel('Number of Cores', fontsize=14)
			plt.ylabel('Scalability', fontsize=14)
			plt.xticks(fontsize=12)
			plt.yticks(fontsize=12)
			plt.legend(loc='best', fontsize=14)
			plt.title(str(args.benchmark_name)  + " size " + str(problemsize) + ((" basecase " + str(basecase)) if int(basecase) > 0 else ""), fontsize=18)
			plt.savefig((str(args.output_dir) + "/") + args.benchmark_name + "-" + str(problemsize) + ("-" + str(basecase) if basecase > 0 else "") + "-scalability.png")
			plt.savefig((str(args.output_dir) + "/") + args.benchmark_name + "-" + str(problemsize) + ("-" + str(basecase) if basecase > 0 else "") + "-scalability.svg")
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
	print("OpenCilk")
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
	vanilla_comparison_str = "OpenCilk vs."
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

def print_latex_for_table():
	# print(f"\\texttt{{ Benchmark }} &  & \\texttt{{ Runtime }} Base Case  & T1 & T2 & T5 & T10 & T20 & T40 \\\\")
	for problemsize in datasets['Serial'].keys():
		for basecase in datasets['Serial'][problemsize][1].keys():
			for runtime in runtime_names:
				if runtime == 'Serial':
					print(f"\\texttt{{ {args.benchmark_name}-{problemsize}{('-' + str(basecase)) if basecase > 0 else '' }}} & \\TimeEntry{{{get_average_or_null(datasets, runtime, problemsize, 1,basecase):.2f}}} & & & & & \\\\")
				else:
					print(f"\\texttt{{ {args.benchmark_name}-{problemsize}{('-' + str(basecase)) if basecase > 0 else '' }-{runtime} }}", end="")
					print(f" & \\TimeEntry{{{get_average_or_null(datasets, runtime, problemsize, 1 ,basecase) if get_average_or_null(datasets, runtime, problemsize, 1 ,basecase)  != '-' else 0:.2f}}}\\SpeedUpEntry{{{get_average_or_null(datasets, 'Serial', problemsize, 1,basecase) / get_average_or_null(datasets, runtime, problemsize, 1,basecase) if get_average_or_null(datasets, runtime, problemsize, 1,basecase) != '-' else 0:.2f}}}", end="")
					print(f" & \\TimeEntry{{{get_average_or_null(datasets, runtime, problemsize, 2,basecase) if get_average_or_null(datasets, runtime, problemsize, 2 ,basecase)  != '-' else 0:.2f}}}\\SpeedUpEntry{{{get_average_or_null(datasets, 'Serial', problemsize, 1,basecase)/get_average_or_null(datasets, runtime, problemsize, 2,basecase) if get_average_or_null(datasets, runtime, problemsize, 2,basecase) != '-' else 0:.2f}}} ", end="")
					print(f" & \\TimeEntry{{{get_average_or_null(datasets, runtime, problemsize, 5,basecase) if get_average_or_null(datasets, runtime, problemsize, 5 ,basecase)  != '-' else 0:.2f}}}\\SpeedUpEntry{{{get_average_or_null(datasets, 'Serial', problemsize, 1,basecase)/get_average_or_null(datasets, runtime, problemsize, 5,basecase) if get_average_or_null(datasets, runtime, problemsize, 5,basecase) != '-' else 0:.2f}}}", end="")
					print(f" & \\TimeEntry{{{get_average_or_null(datasets, runtime, problemsize, 10,basecase) if get_average_or_null(datasets, runtime, problemsize, 10 ,basecase)  != '-' else 0:.2f}}}\\SpeedUpEntry{{{get_average_or_null(datasets, 'Serial', problemsize, 1,basecase)/get_average_or_null(datasets, runtime, problemsize, 10,basecase) if get_average_or_null(datasets, runtime, problemsize, 10,basecase) != '-' else 0:.2f}}} ", end="")
					print(f" & \\TimeEntry{{{get_average_or_null(datasets, runtime, problemsize, 20,basecase) if get_average_or_null(datasets, runtime, problemsize, 20 ,basecase)  != '-' else 0:.2f}}}\\SpeedUpEntry{{{get_average_or_null(datasets, 'Serial', problemsize, 1,basecase)/get_average_or_null(datasets, runtime, problemsize, 20,basecase) if get_average_or_null(datasets, runtime, problemsize, 20,basecase) != '-' else 0:.2f}}} ", end="")
					print(f" & \\TimeEntry{{{get_average_or_null(datasets, runtime, problemsize, 40,basecase) if get_average_or_null(datasets, runtime, problemsize, 40 ,basecase)  != '-' else 0:.2f}}}\\SpeedUpEntry{{{get_average_or_null(datasets, 'Serial', problemsize, 1,basecase)/get_average_or_null(datasets, runtime, problemsize, 40,basecase) if get_average_or_null(datasets, runtime, problemsize, 40,basecase) != '-' else 0:.2f}}} \\\\")
			print("\\hline")


def print_stddev_for_table():
	# print(f"\\texttt{{ Benchmark }} &  & \\texttt{{ Runtime }} Base Case  & T1 & T2 & T5 & T10 & T20 & T40 \\\\")
	for problemsize in datasets['Serial'].keys():
		for basecase in datasets['Serial'][problemsize][1].keys():
			for runtime in runtime_names:
				if runtime == 'Serial':
					print(f"\\texttt{{ {args.benchmark_name}-{problemsize}{('-' + str(basecase)) if basecase > 0 else '' }}} & {get_stddev_or_null(datasets, runtime, problemsize, 1,basecase):.2f} & & & & & \\\\")
				else:
					print(f"\\texttt{{ {args.benchmark_name}-{problemsize}{('-' + str(basecase)) if basecase > 0 else '' }-{runtime} }}", end="")
					print(f" & {get_stddev_or_null(datasets, runtime, problemsize, 1 ,basecase) if get_average_or_null(datasets, runtime, problemsize, 1 ,basecase)  != '-' else 0:.2f}", end='')
					print(f" & {get_stddev_or_null(datasets, runtime, problemsize, 2,basecase) if get_average_or_null(datasets, runtime, problemsize, 2 ,basecase)  != '-' else 0:.2f}", end='')
					print(f" & {get_stddev_or_null(datasets, runtime, problemsize, 5,basecase) if get_average_or_null(datasets, runtime, problemsize, 5 ,basecase)  != '-' else 0:.2f}", end='')
					print(f" & {get_stddev_or_null(datasets, runtime, problemsize, 10,basecase) if get_average_or_null(datasets, runtime, problemsize, 10 ,basecase)  != '-' else 0:.2f}", end='')
					print(f" & {get_stddev_or_null(datasets, runtime, problemsize, 20,basecase) if get_average_or_null(datasets, runtime, problemsize, 20 ,basecase)  != '-' else 0:.2f}", end='')
					print(f" & {get_stddev_or_null(datasets, runtime, problemsize, 40,basecase) if get_average_or_null(datasets, runtime, problemsize, 40 ,basecase)  != '-' else 0:.2f}")
			print("\\hline")

plot_all_problemsize_runtimes()
plot_all_problemsize_speedup()
plot_all_problemsize_scalability()
# print_latex_for_table()
# print_stddev_for_table()
# print_runtimes_as_csv()
