#!/usr/bin/env bash
#
# download_datasets.sh
#
# Reserved for a future shared Open4D dataset registry. It is intentionally not
# implemented yet: there is no single canonical dataset index for the repository.
#
# To fetch an individual externally hosted dataset or checkpoint today, use:
#
#   ./scripts/fetch_artifact.sh URL SHA256 DESTINATION
#
# See docs/artifacts.md for the artifact and reproducibility policy.
set -euo pipefail

echo "download_datasets.sh is not implemented yet." >&2
echo "Use ./scripts/fetch_artifact.sh URL SHA256 DESTINATION to fetch a single" >&2
echo "artifact, or see docs/artifacts.md for the dataset policy." >&2
exit 1
