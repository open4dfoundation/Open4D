#include "mg_precompute_block.h"

void mg_precompute_block(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
  std::vector<mg_data> & mg)
{
	int decimation_type = 1; // default: mid point decimation
	mg_precompute_block(Vf,Ff,decimation_type, mg);
}

void mg_precompute_block(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
	const int & dec_type,
  std::vector<mg_data> & mg)
{
	float coarsening_ratio = 0.25;
	int min_coarsest_nV = 500;
	mg_precompute_block(Vf,Ff,coarsening_ratio,min_coarsest_nV,dec_type, mg);
}

void mg_precompute_block(
	const Eigen::MatrixXd & Vf,
	const Eigen::MatrixXi & Ff,
	const float & ratio,
	const int & nVCoarsest,
	const int & dec_type,
  std::vector<mg_data> & mg)
{
	using namespace std;
	using namespace Eigen;

	// compute number of multigrid levels
	int nLvs;
	{
		nLvs = 1;
		float nV = Vf.rows();
		while (true)
		{
			nV *= ratio;
			if (nV > nVCoarsest)
				nLvs += 1;
			else
				break;				
		}
		// TODO: handle the case where we only have 1 level		
	}

	mg_data data_lv0;
	if (mg.size() > 0) {
		data_lv0 = mg[0];
	}
	else {
		data_lv0.V = Vf;
		data_lv0.F = Ff;
	}

	mg.clear();
	mg.reserve(nLvs);

	// initialize the first level
	mg.push_back(data_lv0);

	for (int lv = 1; lv < nLvs; lv++)
	{
		int tarF = round((float)(mg[lv-1].F.rows()) * ratio);
		cout << "lv: " << lv << ", tarF: " << tarF << endl;

		MatrixXd V = mg[lv-1].V;
		MatrixXi F = mg[lv-1].F;

		SparseMatrix<double> P;
		MatrixXd Vc;
		MatrixXi Fc;
		get_prolong_block(V,F,tarF, dec_type, Vc,Fc,P);
		cout << "lv: " << lv << ", Vc: " << Vc.rows() << endl;

		mg_data data;
		data.V = Vc;
		data.F = Fc;
		data.P = P;
		data.PT = P.transpose();
		data.P_full = P;
		mg.push_back(data);
	}


	// print multigrid info
	cout << "============\n";
	cout << "Multigrid Info\n";
	cout << "============\n";
	cout << "numLv: " << mg.size() << endl;
	cout << "|V_coarsest|: " << mg[mg.size()-1].V.rows() << endl;
}