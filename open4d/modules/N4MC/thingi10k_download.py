import thingi10k

thingi10k.init(cache_dir="/mnt/datadrive/ChromeDownloads/Thingi10K") # Download the dataset and update cache


# Loop through all entries in the dataset
for entry in thingi10k.dataset(num_vertices=(10000, None), closed=True):
    vertices, facets = thingi10k.load_file(entry['file_path'])
