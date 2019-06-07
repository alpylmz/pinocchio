//
// Copyright (c) 2019 INRIA
//

#include "pinocchio/fwd.hpp"
#include "pinocchio/multibody/joint/joint-generic.hpp"
#include "pinocchio/multibody/liegroup/liegroup.hpp"
#include "pinocchio/multibody/liegroup/liegroup-algo.hpp"

#include <casadi/casadi.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/utility/binary.hpp>

BOOST_AUTO_TEST_SUITE(BOOST_TEST_MODULE)

BOOST_AUTO_TEST_CASE(test_jointRX_motion_space)
{
  typedef casadi::SX AD_double;
  typedef pinocchio::JointCollectionDefaultTpl<AD_double> JointCollectionAD;
  typedef pinocchio::JointCollectionDefaultTpl<double> JointCollection;
  
  typedef pinocchio::SE3Tpl<AD_double> SE3AD;
  typedef pinocchio::MotionTpl<AD_double> MotionAD;
  typedef pinocchio::SE3Tpl<double> SE3;
  typedef pinocchio::MotionTpl<double> Motion;
  typedef pinocchio::ConstraintTpl<Eigen::Dynamic,double> ConstraintXd;
  
  typedef Eigen::Matrix<AD_double,Eigen::Dynamic,1> VectorXAD;
  typedef Eigen::Matrix<AD_double,6,1> Vector6AD;
  typedef Eigen::Matrix<AD_double,3,1> Vector3AD;
  typedef Eigen::Matrix<AD_double,9,1> Vector9AD;
  typedef Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> MatrixX;
  
  typedef JointCollectionAD::JointModelRX JointModelRXAD;
  typedef JointModelRXAD::ConfigVector_t ConfigVectorAD;
//  typedef JointModelRXAD::TangentVector_t TangentVectorAD;
  typedef JointCollectionAD::JointDataRX JointDataRXAD;
  
  typedef JointCollection::JointModelRX JointModelRX;
  typedef JointModelRX::ConfigVector_t ConfigVector;
  typedef JointModelRX::TangentVector_t TangentVector;
  typedef JointCollection::JointDataRX JointDataRX;
  
  JointModelRX jmodel; jmodel.setIndexes(0,0,0);
  JointDataRX jdata(jmodel.createData());
  
  JointModelRXAD jmodel_ad = jmodel.cast<AD_double>();
  JointDataRXAD jdata_ad(jmodel_ad.createData());
  
  typedef pinocchio::LieGroup<JointModelRX>::type JointOperation;
  ConfigVector q(jmodel.nq()); JointOperation().random(q);
  
  casadi::SX cs_q = casadi::SX::sym("q", jmodel.nq());
  ConfigVectorAD q_ad(jmodel.nq());
  for(Eigen::DenseIndex k = 0; k < jmodel.nq(); ++k)
  {
    q_ad[k] = cs_q(k);
  }
  
  // Zero order
  jmodel_ad.calc(jdata_ad,q_ad);
  jmodel.calc(jdata,q);
  
  SE3 M1(jdata.M);
  SE3AD M2(jdata_ad.M);

  casadi::SX cs_trans(3,1);
  for(Eigen::DenseIndex k = 0; k < 3; ++k)
  {
    cs_trans(k) = M2.translation()[k];
  }
  casadi::SX cs_rot(3,3);
  for(Eigen::DenseIndex i = 0; i < 3; ++i)
  {
    for(Eigen::DenseIndex j = 0; j < 3; ++j)
    {
      cs_rot(i,j) = M2.rotation()(i,j);
    }
  }
  
  casadi::Function eval_placement("eval_placement", casadi::SXVector {cs_q}, casadi::SXVector {cs_trans,cs_rot});
  std::cout << "Joint Placement = " << eval_placement << std::endl;
  
  std::vector<double> q_vec((size_t)jmodel.nq());
  Eigen::Map<ConfigVector>(q_vec.data(),jmodel.nq(),1) = q;
  casadi::DMVector res = eval_placement(casadi::DMVector {q_vec});
  std::cout << "M(q)=" << res << std::endl;
  
  BOOST_CHECK(M1.translation().isApprox(Eigen::Map<SE3::Vector3>(res[0]->data())));
  BOOST_CHECK(M1.rotation().isApprox(Eigen::Map<SE3::Matrix3>(res[1]->data())));

  // First order
  casadi::SX cs_v = casadi::SX::sym("v", jmodel.nv());
  TangentVector v(TangentVector::Random(jmodel.nv()));
  VectorXAD v_ad(jmodel_ad.nv());
  
  std::vector<double> v_vec((size_t)jmodel.nv());
  Eigen::Map<TangentVector>(v_vec.data(),jmodel.nv(),1) = v;

  for(Eigen::DenseIndex k = 0; k < jmodel.nv(); ++k)
  {
    v_ad[k] = cs_v(k);
  }
  
  jmodel.calc(jdata,q,v);
  Motion m(jdata.v);
  ConstraintXd Sref(jdata.S.matrix());
  
  jmodel_ad.calc(jdata_ad,q_ad,v_ad);
  Vector6AD Y;
  MotionAD m_ad(jdata_ad.v);
  
  casadi::SX cs_vel(6,1);
  for(Eigen::DenseIndex k = 0; k < 6; ++k)
  {
    cs_vel(k) = m_ad.toVector()[k];
  }
  casadi::Function eval_velocity("eval_velocity", casadi::SXVector {cs_q,cs_v}, casadi::SXVector {cs_vel});
  std::cout << "Joint Velocity = " << eval_velocity << std::endl;
  
  casadi::DMVector res_vel = eval_velocity(casadi::DMVector {q_vec,v_vec});
  std::cout << "v(q,v)=" << res_vel << std::endl;
  
  BOOST_CHECK(m.linear().isApprox(Eigen::Map<Motion::Vector3>(res_vel[0]->data())));
  BOOST_CHECK(m.angular().isApprox(Eigen::Map<Motion::Vector3>(res_vel[0]->data()+3)));
  
  casadi::SX dvel_dv = jacobian(cs_vel, cs_v);
  casadi::Function eval_S("eval_S", casadi::SXVector {cs_q,cs_v}, casadi::SXVector {dvel_dv});
  std::cout << "S = " << eval_S << std::endl;
  
  casadi::DMVector res_S = eval_S(casadi::DMVector {q_vec,v_vec});
  std::cout << "res_S:" << res_S << std::endl;
  ConstraintXd::DenseBase Sref_mat = Sref.matrix();
  
  for(Eigen::DenseIndex i = 0; i < 6; ++i)
  {
    for(Eigen::DenseIndex j = 0; i < Sref.nv(); ++i)
      BOOST_CHECK(std::fabs(Sref_mat(i,j) - (double)res_S[0](i,j)) <= Eigen::NumTraits<double>::dummy_precision());
  }
}

struct TestADOnJoints
{
  template<typename JointModel>
  void operator()(const pinocchio::JointModelBase<JointModel> &) const
  {
    JointModel jmodel;
    jmodel.setIndexes(0,0,0);
    
    test(jmodel);
  }
  
  template<typename Scalar, int Options>
  void operator()(const pinocchio::JointModelRevoluteUnalignedTpl<Scalar,Options> & ) const
  {
    typedef pinocchio::JointModelRevoluteUnalignedTpl<Scalar,Options> JointModel;
    typedef typename JointModel::Vector3 Vector3;
    JointModel jmodel(Vector3::Random().normalized());
    jmodel.setIndexes(0,0,0);
    
    test(jmodel);
  }
  
  template<typename Scalar, int Options>
  void operator()(const pinocchio::JointModelPrismaticUnalignedTpl<Scalar,Options> & ) const
  {
    typedef pinocchio::JointModelPrismaticUnalignedTpl<Scalar,Options> JointModel;
    typedef typename JointModel::Vector3 Vector3;
    JointModel jmodel(Vector3::Random().normalized());
    jmodel.setIndexes(0,0,0);
    
    test(jmodel);
  }
  
  template<typename Scalar, int Options, template<typename,int> class JointCollection>
  void operator()(const pinocchio::JointModelTpl<Scalar,Options,JointCollection> & ) const
  {
    typedef pinocchio::JointModelRevoluteTpl<Scalar,Options,0> JointModelRX;
    typedef pinocchio::JointModelTpl<Scalar,Options,JointCollection> JointModel;
    JointModel jmodel((JointModelRX()));
    jmodel.setIndexes(0,0,0);
    
    test(jmodel);
  }
  
  template<typename Scalar, int Options, template<typename,int> class JointCollection>
  void operator()(const pinocchio::JointModelCompositeTpl<Scalar,Options,JointCollection> & ) const
  {
    typedef pinocchio::JointModelRevoluteTpl<Scalar,Options,0> JointModelRX;
    typedef pinocchio::JointModelRevoluteTpl<Scalar,Options,1> JointModelRY;
    typedef pinocchio::JointModelCompositeTpl<Scalar,Options,JointCollection> JointModel;
    JointModel jmodel((JointModelRX()));
    jmodel.addJoint(JointModelRY());
    jmodel.setIndexes(0,0,0);
    
    test(jmodel);
  }
  
  template<typename JointModel>
  static void test(const pinocchio::JointModelBase<JointModel> & jmodel)
  {
    typedef casadi::SX AD_double;
    typedef pinocchio::JointCollectionDefaultTpl<AD_double> JointCollectionAD;
    typedef pinocchio::JointCollectionDefaultTpl<double> JointCollection;
    
    typedef pinocchio::SE3Tpl<AD_double> SE3AD;
    typedef pinocchio::MotionTpl<AD_double> MotionAD;
    typedef pinocchio::SE3Tpl<double> SE3;
    typedef pinocchio::MotionTpl<double> Motion;
    typedef pinocchio::ConstraintTpl<Eigen::Dynamic,double> ConstraintXd;
    
    typedef Eigen::Matrix<AD_double,Eigen::Dynamic,1> VectorXAD;
    typedef Eigen::Matrix<AD_double,6,1> Vector6AD;
    typedef Eigen::Matrix<AD_double,3,1> Vector3AD;
    typedef Eigen::Matrix<AD_double,9,1> Vector9AD;
    typedef Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> MatrixX;
    
    typedef typename pinocchio::CastType<AD_double,JointModel>::type JointModelAD;
    typedef typename JointModelAD::JointDataDerived JointDataAD;
    
    typedef typename JointModelAD::ConfigVector_t ConfigVectorAD;
    
    typedef typename JointModel::JointDataDerived JointData;
    typedef typename JointModel::ConfigVector_t ConfigVector;
    typedef typename JointModel::TangentVector_t TangentVector;
    

    JointData jdata(jmodel.createData());
    pinocchio::JointDataBase<JointData> & jdata_base = jdata;
    
    JointModelAD jmodel_ad = jmodel.template cast<AD_double>();
    JointDataAD jdata_ad(jmodel_ad.createData());
    pinocchio::JointDataBase<JointDataAD> & jdata_ad_base = jdata_ad;
    
    ConfigVector q(jmodel.nq());
    ConfigVector lb(ConfigVector::Constant(jmodel.nq(),-1.));
    ConfigVector ub(ConfigVector::Constant(jmodel.nq(),1.));
    
    typedef pinocchio::RandomConfigurationStep<pinocchio::LieGroupMap,ConfigVector,ConfigVector,ConfigVector> RandomConfigAlgo;
    RandomConfigAlgo::run(jmodel.derived(),typename RandomConfigAlgo::ArgsType(q,lb,ub));

    
    casadi::SX cs_q = casadi::SX::sym("q", jmodel.nq());
    ConfigVectorAD q_ad(jmodel.nq());
    for(Eigen::DenseIndex k = 0; k < jmodel.nq(); ++k)
    {
      q_ad[k] = cs_q(k);
    }
    
    // Zero order
    jmodel_ad.calc(jdata_ad,q_ad);
    jmodel.calc(jdata,q);
    
    SE3 M1(jdata_base.M());
    SE3AD M2(jdata_ad_base.M());
    
    casadi::SX cs_trans(3,1);
    for(Eigen::DenseIndex k = 0; k < 3; ++k)
    {
      cs_trans(k) = M2.translation()[k];
    }
    casadi::SX cs_rot(3,3);
    for(Eigen::DenseIndex i = 0; i < 3; ++i)
    {
      for(Eigen::DenseIndex j = 0; j < 3; ++j)
      {
        cs_rot(i,j) = M2.rotation()(i,j);
      }
    }
    
    casadi::Function eval_placement("eval_placement", casadi::SXVector {cs_q}, casadi::SXVector {cs_trans,cs_rot});
    std::cout << "Joint Placement = " << eval_placement << std::endl;
    
    std::vector<double> q_vec((size_t)jmodel.nq());
    Eigen::Map<ConfigVector>(q_vec.data(),jmodel.nq(),1) = q;
    casadi::DMVector res = eval_placement(casadi::DMVector {q_vec});
    std::cout << "M(q)=" << res << std::endl;
    
    BOOST_CHECK(M1.translation().isApprox(Eigen::Map<SE3::Vector3>(res[0]->data())));
    BOOST_CHECK(M1.rotation().isApprox(Eigen::Map<SE3::Matrix3>(res[1]->data())));
    
    // First order
    casadi::SX cs_v = casadi::SX::sym("v", jmodel.nv());
    TangentVector v(TangentVector::Random(jmodel.nv()));
    VectorXAD v_ad(jmodel_ad.nv());
    
    std::vector<double> v_vec((size_t)jmodel.nv());
    Eigen::Map<TangentVector>(v_vec.data(),jmodel.nv(),1) = v;
    
    for(Eigen::DenseIndex k = 0; k < jmodel.nv(); ++k)
    {
      v_ad[k] = cs_v(k);
    }
    
    jmodel.calc(jdata,q,v);
    Motion m(jdata_base.v());
    ConstraintXd Sref(jdata_base.S().matrix());
    
    jmodel_ad.calc(jdata_ad,q_ad,v_ad);
    Vector6AD Y;
    MotionAD m_ad(jdata_ad_base.v());
    
    casadi::SX cs_vel(6,1);
    for(Eigen::DenseIndex k = 0; k < 6; ++k)
    {
      cs_vel(k) = m_ad.toVector()[k];
    }
    casadi::Function eval_velocity("eval_velocity", casadi::SXVector {cs_q,cs_v}, casadi::SXVector {cs_vel});
    std::cout << "Joint Velocity = " << eval_velocity << std::endl;
    
    casadi::DMVector res_vel = eval_velocity(casadi::DMVector {q_vec,v_vec});
    std::cout << "v(q,v)=" << res_vel << std::endl;
    
    BOOST_CHECK(m.linear().isApprox(Eigen::Map<Motion::Vector3>(res_vel[0]->data())));
    BOOST_CHECK(m.angular().isApprox(Eigen::Map<Motion::Vector3>(res_vel[0]->data()+3)));
    
    casadi::SX dvel_dv = jacobian(cs_vel, cs_v);
    casadi::Function eval_S("eval_S", casadi::SXVector {cs_q,cs_v}, casadi::SXVector {dvel_dv});
    std::cout << "S = " << eval_S << std::endl;
    
    casadi::DMVector res_S = eval_S(casadi::DMVector {q_vec,v_vec});
    std::cout << "res_S:" << res_S << std::endl;
    ConstraintXd::DenseBase Sref_mat = Sref.matrix();
    
    for(Eigen::DenseIndex i = 0; i < 6; ++i)
    {
      for(Eigen::DenseIndex j = 0; i < Sref.nv(); ++i)
        BOOST_CHECK(std::fabs(Sref_mat(i,j) - (double)res_S[0](i,j)) <= Eigen::NumTraits<double>::dummy_precision());
    }
  }
};

BOOST_AUTO_TEST_CASE(test_all_joints)
{
  typedef pinocchio::JointCollectionDefault::JointModelVariant JointModelVariant;
  boost::mpl::for_each<JointModelVariant::types>(TestADOnJoints());

  TestADOnJoints()(pinocchio::JointModel());
}

BOOST_AUTO_TEST_SUITE_END()
