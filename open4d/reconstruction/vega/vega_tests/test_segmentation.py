import torch

from vega.segmentation import clustering_purity, kmeans, match_centers


def test_kmeans_recovers_well_separated_clusters():
    torch.manual_seed(0)
    centers_true = torch.tensor([[-10.0, 0, 0], [10.0, 0, 0], [0, 10.0, 0]], device="cuda")
    pts = torch.cat([c + torch.randn(50, 3, device="cuda") * 0.1 for c in centers_true], dim=0)
    true_labels = torch.cat([torch.full((50,), i) for i in range(3)])

    labels, _ = kmeans(pts, k=3, n_iters=30, seed=0)
    purity = clustering_purity(labels, true_labels)
    assert purity > 0.95


def test_match_centers_is_identity_for_unmoved_centers():
    centers = torch.tensor([[0.0, 0, 0], [10.0, 0, 0], [20.0, 0, 0]])
    mapping = match_centers(centers, centers)
    assert mapping.tolist() == [0, 1, 2]


def test_match_centers_recovers_permutation():
    centers = torch.tensor([[0.0, 0, 0], [10.0, 0, 0], [20.0, 0, 0]])
    shuffled = centers[[2, 0, 1]]  # shuffled order
    mapping = match_centers(centers, shuffled)
    # shuffled[0] (=centers[2]) should map back to prev index 2, etc.
    assert mapping.tolist() == [2, 0, 1]
