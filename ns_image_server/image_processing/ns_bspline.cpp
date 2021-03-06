#include "ns_ex.h"
#include "ns_bspline.h"
#include "Wm4BSplineCurve2.h"
#include "Wm4BSplineFitBasis.h"
#include "Wm4BSplineCurveFit.h"
using namespace std;
using namespace Wm4;
class ns_bspline_internal{
public:
	ns_bspline_internal():m_pkSpline(0){}
	~ns_bspline_internal(){	if (m_pkSpline != 0) delete m_pkSpline;}

	void calculate(const unsigned int degree, const bool open_spline, const bool connect_spline_endpoints, 
		const unsigned int input_count, const Vector2f * input){
		m_pkSpline = WM4_NEW BSplineCurve2f(input_count,input,degree,connect_spline_endpoints,open_spline);
	}

	void generate_all_stats(const unsigned int output_count, std::vector<ns_vector_2d> & positions, std::vector<ns_vector_2d> & tangents, std::vector<double> & curvature, double & length){
		positions.resize(output_count);
		tangents.resize(output_count);
		curvature.resize(output_count);
		for (unsigned int i = 0; i < output_count; i++){
			float t(((float)i)/(float)(output_count-1));
			Vector2f kPos,kTan,kGrad;

			m_pkSpline->Get(t, &kPos,&kTan, &kGrad, 0);
			positions[i] = ns_vector_2d(kPos.X(),kPos.Y());
			tangents[i]  = ns_vector_2d(kTan.X(),kTan.Y());

			float fSpeedSqr = kTan.SquaredLength();

			if (fSpeedSqr >= Wm4::Math<float>::ZERO_TOLERANCE){
				float fNumer = kTan.DotPerp(kGrad);
				float fDenom = Math<float>::Pow(fSpeedSqr,(float)1.5);
				curvature[i] =  fNumer/fDenom;
			}
			else// curvature is indeterminate, just return 0
				curvature[i] = 0.0;
		}
		length = 0;
		ns_vector_2d prev = positions[0];
		for (unsigned int i = 1; i < positions.size(); i++){
			length+=(positions[i]-prev).mag();
			prev = positions[i];
		}
		//length = m_pkSpline->GetLength(0.0,1.0);
		//cerr << "\n";
	}
	private:
	BSplineCurve2f * m_pkSpline;
};

class ns_bspline_fitter_internal{
public:
	ns_bspline_fitter_internal():fitter(0){}
	~ns_bspline_fitter_internal(){	if (fitter != 0) delete fitter;}

	void calculate(const unsigned int degree, const unsigned int control_point_number, const unsigned int input_count, const float * data){
		
		degree_ = degree;
		control_point_number_ = control_point_number;
		input_count_ = input_count;

    if (1 > degree)
		throw ns_ex("Degree too small");
	if ( degree >= control_point_number)
		throw ns_ex("Control quantity less than degree!");
    if (control_point_number > input_count)
		throw ns_ex("Control quanitity greater than sample quantity");


		fitter = new BSplineCurveFit<float>(2, input_count, data, degree,control_point_number);
	}

	void generate_bspline(ns_bspline& bspline){
		Vector2f * cp;
		const float * control_points = fitter->GetControlData();

		cp = new Vector2f[control_point_number_];
		for (unsigned int i = 0; i < control_point_number_; i++){
			cp[i].X() = control_points[2*i];
			cp[i].Y() = control_points[2*i+1];
		}
		
		try{
			bspline.calculate_intersecting_all_points(degree_,true,false,control_point_number_,static_cast<void *>(cp),input_count_);
			delete[] cp;
		}
		catch(...){
			delete[] cp;
			throw;
		}
	}

	private:
	unsigned int degree_,control_point_number_,input_count_;
	BSplineCurveFit<float> * fitter;
};

unsigned long ns_bspline::crop_ends(const double crop_fraction){
	int new_start(0), 
		new_end((int)positions.size()-1);
	if (positions.size() < 2)
		return 0;
		//throw ns_ex("ns_bspline::Cannot crop tiny segment: ") << (unsigned long)positions.size();
	//cut off the ends
	if (length != 0){
		double chop_length(crop_fraction*length);	
		double start_len(0),end_len(0);
		
		ns_vector_2d prev(positions[new_start]);
		for (;;){
			start_len+=(positions[new_start]-prev).mag();
			if (start_len >= chop_length || new_start == positions.size()) break;
			prev = positions[new_start];
			new_start++;
		}	
		new_start--;
		prev = positions[new_end];
		for (;;){
			end_len+=(positions[new_end]-prev).mag();
			if (end_len >= chop_length || new_end == 0) break;
			prev = positions[new_end];
			new_end--;
		}
		new_end++;
		//cerr << "Cropping from [0," << positions.size() << "] to [" << new_start << "," << new_end << "]";
		//cerr << "sl:" << positions[positions.size()-2] << ", " << positions[positions.size()-1] << "\n";
		ns_crop_vector(positions,new_start, new_end-new_start+1);
		ns_crop_vector(tangents,new_start, new_end-new_start+1);
		ns_crop_vector(curvature,new_start, new_end-new_start+1);
	}
	return new_start;
}
#define NS_BSPLINE_STANDARD_DEGREE 8
void ns_bspline::calculate_with_standard_params(const std::vector<ns_vector_2d> & data,unsigned int output_size,const bool smoother){
	if (data.size() == 0)
		throw ns_ex("ns_bspline::calculate_with_standard_params()::Cannot calculate spine information with no data points!");
	if (data.size() == 1)
		throw ns_ex("ns_bspline::calculate_with_standard_params()::Cannot calculate spine information for a single data point!");
	if (data.size() < 4){
		calculate_intersecting_all_points((unsigned int)data.size()-1,true,false,data,output_size);
		return;
	}
//	unsigned int count (1000);
//	unsigned long fit_start = ns_current_time();
//	for (unsigned int i = 0; i < count; i++){
		ns_bspline_fitter_internal fitter;

		float * dat = new float[2*data.size()];
		try{
			for (unsigned int i = 0; i < data.size(); i++){
				dat[2*i] = (float)data[i].x;
				dat[2*i+1] = (float)data[i].y;
			}
			unsigned long degree = NS_BSPLINE_STANDARD_DEGREE;
			/*if (degree > 24)
				degree = 24;
			if (degree < 8)
				degree = 8;*/
			long control_point_ratio;
			if (smoother)
				control_point_ratio=75;
			else control_point_ratio=10;
			unsigned long component((unsigned long)data.size()/control_point_ratio);
			if(component < 3*(degree/2))
				component = 3*(degree/2);
			if (data.size() < 12){
				degree = (unsigned long)data.size()-3;
				component = (unsigned long)data.size()-1;
			}
		//	cerr << "\nDegree: " << degree << "\n";
			//if (data.size() < 12)
			//	cerr << "d:" << degree << " c:" << component << " dta: " << data.size() << "\n";
			fitter.calculate(degree,component,(unsigned long)data.size(),dat);
			fitter.generate_bspline(*this);
			delete[] dat;
		}
		catch(...){
			delete[] dat;	
			throw;
		}
//	}
//	unsigned long fit_stop = ns_current_time();
//	cerr << "Fitting: " << fit_stop - fit_start;

/*	unsigned long degree_start = ns_current_time();
	for (unsigned int i = 0; i < count; i++){
		if (output_size < 2)
			output_size = 2;
		if (data.size() <= 8){
			calculate_intersecting_all_points((unsigned int)data.size()-1,true,false,data,output_size);
			return;
		}
		calculate_intersecting_all_points((unsigned int)data.size()-8,true,false,data,output_size);
	}
	unsigned long degree_stop = ns_current_time();
	cerr << "High Degree: " << degree_stop - degree_start << "\n";*/

}


void ns_bspline::calculate_intersecting_all_points(const unsigned int degree, const bool open_spline, const bool connect_spline_endpoints, const unsigned long data_count, const void * d, unsigned int output_size = 0){
	const Vector2f * data = static_cast<const Vector2f *>(d);
	if (output_size == 0) output_size = (unsigned int)data_count;
	if (degree < 1)
		throw ns_ex("ns_bspline::calculate()::Requesting a spline of degree") << degree << "; # Data points:" << (unsigned int)data_count;
	if (degree > data_count-1)
		throw ns_ex("ns_bspline::calculate()::Requesting a spline of degree") << degree << " with only " << (unsigned int)data_count << " data points";
	degree_used_ = degree;

	ns_bspline_internal bspline;

	bspline.calculate(degree,open_spline,connect_spline_endpoints,data_count,data);
	
	bspline.generate_all_stats(output_size,positions,tangents,curvature,length);
}

void ns_bspline::calculate_intersecting_all_points(const unsigned int degree, const bool open_spline, const bool connect_spline_endpoints, const std::vector<ns_vector_2d> & data, unsigned int output_size = 0){

	Vector2f * dat = new Vector2f[data.size()];
	try{
		for (unsigned int i = 0; i < data.size(); i++){
			dat[i].X() = (float)data[i].x;
			dat[i].Y() = (float)data[i].y;
		}
		calculate_intersecting_all_points(degree,open_spline,connect_spline_endpoints,(const unsigned int)data.size(),dat,output_size);
	}
	catch(...){
		delete[] dat;	
		throw;
	}
	delete[] dat;
}
