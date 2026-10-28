import pandas as pd
import matplotlib.pyplot as plt
import os
import argparse

# Set up argument parser
parser = argparse.ArgumentParser(description='Generate stacked area graph and average time charts for a given benchmark.')
parser.add_argument('benchmark_name', type=str, help='Name of the benchmark (e.g., lu, heat)')
args = parser.parse_args()

# Read the benchmark name from the command-line argument
benchmark_name = args.benchmark_name

# Columns to include in the plot
included_columns = ['working', 'scheduling', 'idling', 'sleep (sched)', 'sleep (uncilk)', 'backtrack', 'interupt']

# Create a figures directory if it doesn't exist
if not os.path.exists('figures'):
    os.makedirs('figures')

# Construct file names based on the benchmark name
input_file = f'{benchmark_name}_core_summary.csv'
output_file = f'figures/{benchmark_name}_time_summary.png'

# Read the CSV file
try:
    df = pd.read_csv(input_file, index_col=False)

    # Ensure 'core_count' column is present and set it as the index
    if 'core_count' not in df.columns:
        print("Error: 'core_count' column not found.")
        exit(1)
    df['core_count'] = df['core_count'].astype(int)  # Convert core_count to integers
    df.set_index('core_count', inplace=True)

except FileNotFoundError:
    print(f"Error: File '{input_file}' not found.")
    exit(1)
except ValueError as e:
    print(f"Error: {e}")
    exit(1)

df['scheduling'] = df['scheduling']-df['backtrack']

# Calculate the required metrics
df['avg_idle_time'] = df['idling'] / df['idling count']
df['avg_backtracking_time'] = df['backtrack'] / df['backtrack count']
df['steals_ratio'] = df['success'] / df['failure'].replace(0, 1)  # Avoid division by zero
df['avg_work_time'] = df['working'] / df['working count']

# Selecting only the specified columns
selected_columns = [col for col in included_columns if col in df.columns]

# Check if selected columns are empty
if not selected_columns:
    print("Error: None of the specified columns were found in the data.")
    exit(1)

# Create a figure with subplots
fig, axes = plt.subplots(nrows=2, ncols=2, figsize=(20, 10))  # Adjust for a square figure

# Flatten axes array for easy indexing
axes = axes.flatten()

idle_time_fields = ['f_frme_type', 'f_true_heur', 'f_deque_emp', 'f_bcktk_emp', 
                    'f_grdy_heur', 'f_clsre_lck', 'f_deque_lck', 'steal_cache', 'stealing']

selected_idle_time_fields = [col for col in idle_time_fields if col in df.columns]

count_fields_suffix = ' count'  # Suffix used for count fields

# Ensure the fields exist in the DataFrame and calculate averages
for field in idle_time_fields:
    count_field = field + count_fields_suffix
    if field not in df.columns or count_field not in df.columns:
        continue
    df[field + '_avg'] = df[field] / df[count_field].replace(0, 1)  # Avoid division by zero

# Create average fields list
average_fields = [field + '_avg' for field in idle_time_fields if field in df.columns]



# Stacked area graph
axes[0].stackplot(df.index.values, df[selected_columns].to_numpy().T, labels=selected_columns)
axes[0].set_xlabel('Core Count')
axes[0].set_ylabel('Time Spent Executing')
axes[0].set_title('Stacked Area Graph of Execution Time by Core Count')
axes[0].legend(loc='upper left')

# Average idle time
axes[1].stackplot(df.index.values, [df[field].to_numpy().T for field in idle_time_fields], labels=idle_time_fields)
axes[1].set_xlabel('Core Count')
axes[1].set_ylabel('Time Spent Executing')
axes[1].set_title('Stacked Area Graph of Idle Time by Core Count')
axes[1].legend(loc='upper left')

# Average time per item
axes[2].set_prop_cycle(color=plt.cm.tab10.colors)  # Set a color cycle for clarity
for avg_field in average_fields:
    axes[2].plot(df.index.values, df[avg_field].to_numpy(), label=avg_field)
axes[2].set_xlabel('Core Count')
axes[2].set_ylabel('Average Time per Execution')
axes[2].set_title('Average Time per Execution in Idle Time Fields')
axes[2].legend(loc='upper left')



# Successful steals per failed steal
axes[3].plot(df.index.values[1:], df['avg_work_time'].to_numpy()[1:], label='Average Work Time per Steal', color='blue')
axes[3].set_xlabel('Core Count')
axes[3].set_ylabel('Work per Work Segment')
axes[3].set_title('Average Work Time per Steal')
axes[3].legend(loc='upper left')
axes[3].set_ylim(0)

# Adjust layout
plt.tight_layout()
plt.title(str(args.benchmark_name))
# Save the plot in the figures directory
plt.savefig(output_file, format='png')

# Optionally display the plot
# plt.show()

print(f"Charts saved as '{output_file}' in 'figures' directory.")