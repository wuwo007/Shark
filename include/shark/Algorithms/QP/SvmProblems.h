/*!

 *
 *  \author  T. Glasmachers, O.Krause
 *  \date    2013
 *
 *  \par Copyright (c) 1999-2012:
 *      Institut f&uuml;r Neuroinformatik<BR>
 *      Ruhr-Universit&auml;t Bochum<BR>
 *      D-44780 Bochum, Germany<BR>
 *      Phone: +49-234-32-25558<BR>
 *      Fax:   +49-234-32-14209<BR>
 *      eMail: Shark-admin@neuroinformatik.ruhr-uni-bochum.de<BR>
 *      www:   http://www.neuroinformatik.ruhr-uni-bochum.de<BR>
 *
 *
 *  <BR><HR>
 *  This file is part of Shark. This library is free software;
 *  you can redistribute it and/or modify it under the terms of the
 *  GNU General Public License as published by the Free Software
 *  Foundation; either version 3, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef SHARK_ALGORITHMS_QP_SVMPROBLEMS_H
#define SHARK_ALGORITHMS_QP_SVMPROBLEMS_H

#include <shark/Algorithms/QP/QpSolver.h>

namespace shark{
 
///Working-Set-Selection-Kriteria anwendung:
///Kriterium krit;
/// value=krit(problem,i,j);
struct MVPSelectionCriterion{
	/// \brief Select the most violatig pair (MVP)
	///
	/// \return maximal KKT vioation
	/// \param the svm problem to select the working set for
	/// \param i  first working set component
	/// \param j  second working set component
	template<class Problem>
	double operator()(Problem& problem, std::size_t& i, std::size_t& j)
	{
		double largestUp = -1e100;
		double smallestDown = 1e100;

		for (std::size_t a=0; a < problem.active(); a++)
		{
			double aa = problem.alpha(a);
			double ga = problem.gradient(a);
			if (aa < problem.boxMax(a))
			{
				if (ga > largestUp)
				{
					largestUp = ga;
					i = a;
				}
			}
			if (aa > problem.boxMin(a))
			{
				if (ga < smallestDown)
				{
					smallestDown = ga;
					j = a;
				}
			}
		}

		// MVP stopping condition
		return largestUp - smallestDown;
	}
	
	void reset(){}
};


struct LibSVMSelectionCriterion{
	
	/// \brief Select a working set according to the second order algorithm of LIBSVM 2.8
	///
	/// \return maximal KKT vioation
	/// \param the svm problem to select the working set for
	/// \param i  first working set component
	/// \param j  second working set component
	template<class Problem>
	double operator()(Problem& problem, std::size_t& i, std::size_t& j)
	{
		i = 0;
		j = 1;

		double smallestDown = 1e100;
		double largestUp = -1e100;

		for (std::size_t a=0; a < problem.active(); a++)
		{
			double aa = problem.alpha(a);
			double ga = problem.gradient(a);
			if (aa < problem.boxMax(a))
			{
				if (ga > largestUp)
				{
					largestUp = ga;
					i = a;
				}
			}
		}
		if (largestUp == -1e100) return 0.0;

		// find the second index using second order information
		typename Problem::QpFloatType* q = problem.quadratic().row(i, 0, problem.active());
		double best = 0.0;
		for (std::size_t a = 0; a < problem.active(); a++){
			double aa = problem.alpha(a);
			double ga = problem.gradient(a);
			if (aa > problem.boxMin(a))
			{
				smallestDown=std::min(smallestDown,ga);
				
				double grad_diff = largestUp - ga;
				if (grad_diff > 0.0)
				{
					double quad_coef = problem.diagonal(i) + problem.diagonal(a) - 2.0 * q[a];
					if (quad_coef == 0.0) continue;
					double obj_diff = (grad_diff * grad_diff) / quad_coef;

					if (obj_diff > best)
					{
						best = obj_diff;
						j = a;
					}
				}
			}
		}

		if (best == 0.0) return 0.0;		// numerical accuracy reached :(

		// MVP stopping condition
		return largestUp - smallestDown;
	}
	
	void reset(){}
};

class HMGSelectionCriterion{
public:
	HMGSelectionCriterion():useLibSVM(true),smallProblem(false){}

	/// \brief Select a working set according to the hybrid maximum gain (HMG) algorithm
	///
	/// \return maximal KKT vioation
	///  \param i  first working set component
	///  \param j  second working set component
	/// \param the svm problem to select the working set for
	/// \param i  first working set component
	/// \param j  second working set component
	template<class Problem>
	double operator()(Problem& problem, std::size_t& i, std::size_t& j)
	{
		if (smallProblem || useLibSVM || isInCorner(problem))
		{
			useLibSVM = false;
			if(!smallProblem && sqr(problem.active()) < problem.quadratic().getMaxCacheSize())
				smallProblem = true;
			LibSVMSelectionCriterion libSVMSelection;
			double value = libSVMSelection(problem,i, j);
			last_i = i;
			last_j = j;
			return value;
		}
		//~ //old HMG
		MGStep besti = selectMGVariable(problem,last_i);
		if(besti.violation == 0.0)
			return 0;
		MGStep bestj = selectMGVariable(problem,last_j);
		
		if(bestj.gain > besti.gain){
			i = last_j;
			j = bestj.index;
		}else{
			i = last_i;
			j = besti.index;
		}
		if (problem.gradient(i) < problem.gradient(j)) 
			std::swap(i, j);
		last_i = i;
		last_j = j;
		return besti.violation;
	}
	
	void reset(){
		useLibSVM = true;
		smallProblem = false;
	}
	
private:
	template<class Problem>
	bool isInCorner(Problem const& problem)const{
		double Li = problem.boxMin(last_i);
		double Ui = problem.boxMax(last_i);
		double Lj = problem.boxMin(last_j);
		double Uj = problem.boxMax(last_j);
		double eps_i = 1e-8 * (Ui - Li);
		double eps_j = 1e-8 * (Uj - Lj);
		
		if ((problem.alpha(last_i) <= Li + eps_i || problem.alpha(last_i) >= Ui - eps_i)
		&& ((problem.alpha(last_j) <= Lj + eps_j || problem.alpha(last_j) >= Uj - eps_j)))
		{
			return true;
		}
		return false;
	}
	struct MGStep{
		std::size_t index;//index of variable
		double violation;//computed gradientValue
		double gain;
	};
	template<class Problem>
	MGStep selectMGVariable(Problem& problem,std::size_t i) const{
		
		//best variable pair found so far
		std::size_t bestIndex = 0;//index of variable
		double bestGain = 0;
		
		double largestUp = -1e100;
		double smallestDown = 1e100;

		// try combinations with b = old_i
		typename Problem::QpFloatType* q = problem.quadratic().row(i, 0, problem.active());
		double ab = problem.alpha(i);
		double db = problem.diagonal(i);
		double Lb = problem.boxMin(i);
		double Ub = problem.boxMax(i);
		double gb = problem.gradient(i);
		for (std::size_t a = 0; a < problem.active(); a++)
		{
			double aa = problem.alpha(a);
			double da = problem.diagonal(a);
			double ga = problem.gradient(a);
			double La = problem.boxMin(a);
			double Ua = problem.boxMax(a);
			if (aa < Ua)
				largestUp = std::max(largestUp,ga);
			if (aa > La)
				smallestDown = std::min(smallestDown,ga);
			
			if (a == i) continue;

			double denominator = (da + db - 2.0 * q[a]);
			double mu_max = (ga - gb) / denominator;
			double mu_star = mu_max;

			if (aa + mu_star < La) mu_star = La - aa;
			else if (mu_star + aa > Ua) mu_star = Ua - aa;
			if (ab - mu_star < Lb) mu_star = ab - Lb;
			else if (ab - mu_star > Ub) mu_star = ab - Ub;

			double gain = mu_star * (2.0 * mu_max - mu_star) * denominator;
			
			// select the largest gain
			if (gain > bestGain)
			{
				bestGain = gain;
				bestIndex = a;
			}
		}
		MGStep step;
		step.violation= largestUp-smallestDown;
		step.index = bestIndex;
		step.gain=bestGain;
		return step;
		
	}
	
	std::size_t last_i;
	std::size_t last_j;
	
	bool useLibSVM;
	bool smallProblem;
};


template<class Problem>
class SvmProblem{
public:
	typedef typename Problem::QpFloatType QpFloatType;
	typedef typename Problem::MatrixType MatrixType;
	//typedef LibSVMSelectionCriterion PreferedSelectionStrategy;
	typedef HMGSelectionCriterion PreferedSelectionStrategy;

	SvmProblem(Problem& problem)
	: m_problem(problem)
	, m_gradient(problem.linear)
	, m_active(problem.dimensions()){
		//compute the gradient if alpha != 0
		for (std::size_t i=0; i != dimensions(); i++){
			double v = alpha(i);
			if (v != 0.0){
				QpFloatType* q = quadratic().row(i, 0, dimensions());
				for (std::size_t a=0; a < dimensions(); a++) 
					m_gradient(a) -= q[a] * v;
			}
		}
	}
	std::size_t dimensions()const{
		return m_problem.dimensions();
	}

	std::size_t active()const{
		return m_active;
	}

	double boxMin(std::size_t i)const{
		return m_problem.boxMin(i);
	}
	double boxMax(std::size_t i)const{
		return m_problem.boxMax(i);
	}

	/// representation of the quadratic part of the objective function
	MatrixType& quadratic(){
		return m_problem.quadratic;
	}

	double linear(std::size_t i)const{
		return m_problem.linear(i);
	}

	double alpha(std::size_t i)const{
		return m_problem.alpha(i);
	}

	double diagonal(std::size_t i)const{
		return m_problem.diagonal(i);
	}

	double gradient(std::size_t i)const{
		return m_gradient(i);
	}

	RealVector getUnpermutedAlpha()const{
		RealVector alpha(dimensions());
		for (std::size_t i=0; i<dimensions(); i++) 
			alpha(m_problem.permutation[i]) = m_problem.alpha(i);
		return alpha;
	}

	///\brief Does an update of SMO given a working set with indices i and j.
	void updateSMO(std::size_t i, std::size_t j){
		double ai = alpha(i);
		double aj = alpha(j);
		double Ui = boxMax(i);
		double Lj = boxMin(j);

		// get the matrix rows corresponding to the working set
		QpFloatType* qi = quadratic().row(i, 0, active());
		QpFloatType* qj = quadratic().row(j, 0, active());

		// update alpha, that is, solve the sub-problem defined by i and j
		double numerator = gradient(i) - gradient(j);
		double denominator = diagonal(i) + diagonal(j) - 2.0 * qi[j];
		double mu = numerator / denominator;

		// do the update carefully - avoid numerical problems
		if (mu >= std::min(Ui - ai, aj - Lj))
		{
			if (Ui - ai > aj - Lj)
			{
				mu = aj - Lj;
				m_problem.alpha(i) += mu;
				m_problem.alpha(j) = Lj;
			}
			else if (Ui - ai < aj - Lj)
			{
				mu = Ui - ai;
				m_problem.alpha(i) = Ui;
				m_problem.alpha(j) -= mu;
			}
			else
			{
				mu = Ui - ai;
				m_problem.alpha(i) = Ui;
				m_problem.alpha(j) = Lj;
			}
		}
		else
		{
			m_problem.alpha(i) += mu;
			m_problem.alpha(j) -= mu;
		}

		// update the gradient
		for (std::size_t a = 0; a < active(); a++) 
			m_gradient(a) -= mu * (qi[a] - qj[a]);
	}

	///\brief Returns the current function value of the problem.
	double functionValue()const{
		//std::cout<<m_gradient<<std::endl;
		return 0.5*inner_prod(m_gradient+m_problem.linear,m_problem.alpha);
	}

	bool shrink(double){return false;}
	void reshrink(){}
	void unshrink(){}

	void modifyStep(std::size_t i, std::size_t j, double diff){
		SIZE_CHECK(i < dimensions());
		RANGE_CHECK(alpha(i)+diff >= boxMin(i)-1.e-14*(boxMax(i)-boxMin(i)));
		RANGE_CHECK(alpha(i)+diff <= boxMax(i)+1.e-14*(boxMax(i)-boxMin(i)));
		if(diff == 0) return;

		RANGE_CHECK(alpha(j)-diff >= boxMin(j)-1.e-14*(boxMax(i)-boxMin(i)));
		RANGE_CHECK(alpha(j)-diff <= boxMax(j)+1.e-14*(boxMax(i)-boxMin(i)));

		boundedUpdate(m_problem.alpha(i),diff,boxMin(i),boxMax(i));
		boundedUpdate(m_problem.alpha(j),-diff,boxMin(j),boxMax(j));

		QpFloatType* qi = quadratic().row(i, 0, active());
		QpFloatType* qj = quadratic().row(j, 0, active());

		// update the gradient
		for (std::size_t a = 0; a < active(); a++) 
			m_gradient(a) -= diff * qi[a] - diff * qj[a];
	}

protected:
	Problem& m_problem;

	/// gradient of the objective function at the current alpha
	RealVector m_gradient;	

	std::size_t m_active; 
};

template<class Problem>
struct SvmShrinkingProblem
: public BaseShrinkingProblem<SvmProblem<Problem> >{
	typedef BaseShrinkingProblem<SvmProblem<Problem> > base_type;
	static const std::size_t IterationsBetweenShrinking;

	SvmShrinkingProblem(Problem& problem, bool shrink = true)
	: base_type(problem,shrink)
	, m_isUnshrinked(false)
	, m_shrinkCounter(std::min(this->dimensions(),IterationsBetweenShrinking)){}

protected:
	void doShrink(double epsilon){
		//check if shrinking is necessary
		--m_shrinkCounter;
		if(m_shrinkCounter != 0) return;
		m_shrinkCounter = std::min(this->active(),IterationsBetweenShrinking);

		double largestUp;
		double smallestDown;
		getMaxKKTViolations(largestUp,smallestDown,this->active());

		// check whether unshrinking is necessary at this accuracy level
		// to prevent that a shrinking error invalidates
		// the fine grained late optimization steps
		if (! m_isUnshrinked && (largestUp - smallestDown < 10.0 * epsilon))
		{
			m_isUnshrinked = true;
			this->reshrink();
			return;
		}
		doShrink(largestUp,smallestDown);

		//std::cout<<m_problem.active()<<" "<<std::flush;
	}

	/// \brief Unshrink the problem and immdiately reshrink it.
	void doReshrink(){
		if (this->active() == this->dimensions()) return;

		this->unshrink();

		// shrink directly again
		double largestUp;
		double smallestDown;
		getMaxKKTViolations(largestUp,smallestDown,this->dimensions());
		doShrink(largestUp,smallestDown);

		m_shrinkCounter = std::min(this->active(),IterationsBetweenShrinking);
	}

private:
	void doShrink(double largestUp, double smallestDown){
		for (int a = this->active()-1; a >= 0; --a){
			if(testShrinkVariable(a,largestUp,smallestDown))
				this->shrinkVariable(a);
		}
	}

	bool testShrinkVariable(std::size_t a, double largestUp, double smallestDown)const{
		double v = this->alpha(a);
		double g = this->gradient(a);

		if (
			( g <= smallestDown && v == this->boxMin(a))
			|| ( g >=largestUp && v == this->boxMax(a))
		){
			// In this moment no feasible step including this variable
			// can improve the objective. Thus deactivate the variable.
			return true;
		}
		return false;
	}

	void getMaxKKTViolations(double& largestUp, double& smallestDown, std::size_t maxIndex){
		largestUp = -1e100;
		smallestDown = 1e100;
		for (std::size_t a = 0; a < maxIndex; a++)
		{
			double v = this->alpha(a);
			double g = this->gradient(a);
			if (v > this->boxMin(a))
				smallestDown = std::min(smallestDown,g);
			if (v < this->boxMax(a))
				largestUp = std::max(largestUp,g);
		}
	}

	/// true if the problem has already been unshrinked
	bool m_isUnshrinked;

	std::size_t m_shrinkCounter;
};
template<class Problem>
const std::size_t SvmShrinkingProblem<Problem>::IterationsBetweenShrinking = 1000;

}
#endif