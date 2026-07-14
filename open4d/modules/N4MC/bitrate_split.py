import matplotlib.pyplot as plt
import numpy as np
import os

# -----------------------------------------
# Data: per-component bitrate breakdown (%)
# Extend this dictionary with your datasets
# -----------------------------------------
data = {
    "Dancer": {
        "Features": 73.12,
        "Model": 26.72,
        "Latent Codes": 0.16
    },
    "Basketball Player": {
        "Features": 80.19,
        "Model": 19.77,
        "Latent Codes": 0.14
    },
    "Mitch": {
        "Features": 71.83,
        "Model": 28.06,
        "Latent Codes": 0.11
    },
    "Thomas": {
        "Features": 68.14,
        "Model": 31.67,
        "Latent Codes": 0.19
    },
    "Mixed": {
        "Features": 86.74,
        "Model": 13.14,
        "Latent Codes": 0.12
    },

}
#dancer 4.188 1.529 0.006 5.723
#basketball 5.436 1.34 0.009 6.779
#mitch 3.888 1.519 0.006 5.413
#thomas 3.24 1.506 0.009 4.755
#mixed 4.371 0.662 0.006 5.039
# Output directory
out_dir = "./figures/bitrate_breakdown_plots"
os.makedirs(out_dir, exist_ok=True)

# Global style
plt.rcParams.update({
    "font.size": 28,
    "axes.labelsize": 26,
    "xtick.labelsize": 24,
    "ytick.labelsize": 24,
})

# -----------------------------------------
# Plot each dataset separately
# -----------------------------------------
for dataset_name, components in data.items():

    labels = list(components.keys())
    values = list(components.values())

    plt.figure(figsize=(8, 6))
    bars = plt.bar(labels, values, width=0.4)

    # Annotate bars with exact percentages
    for bar in bars:
        height = bar.get_height()
        plt.text(
            bar.get_x() + bar.get_width()/2,
            height + 1,
            f"{height:.2f}%",
            ha='center',
            va='bottom',
            fontsize=24
        )

    #plt.title(f"Bitrate Breakdown for {dataset_name}")
    plt.ylabel("Percentage (%)")
    plt.ylim(0, 100)

    plt.tight_layout()
    save_path = os.path.join(out_dir, f"{dataset_name.replace(' ', '_').lower()}_breakdown.png")
    plt.savefig(save_path, dpi=300, bbox_inches='tight')
    plt.close()

    print(f"[Saved] {save_path}")