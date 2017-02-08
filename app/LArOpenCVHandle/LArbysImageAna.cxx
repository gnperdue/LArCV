#ifndef __LARBYSIMAGEANA_CXX__
#define __LARBYSIMAGEANA_CXX__
#include "LArbysImageAna.h"

#include "LArUtil/GeometryHelper.h"
#include "LArUtil/LArProperties.h"
#include "DataFormat/EventImage2D.h"
#include "LArOpenCV/ImageCluster/AlgoData/VertexEstimateData.h"

namespace larcv {

  static LArbysImageAnaProcessFactory __global_LArbysImageAnaProcessFactory__;

  LArbysImageAna::LArbysImageAna(const std::string name)
    : ProcessBase(name),
      _event_tree(nullptr),
      _vtx3d_tree(nullptr)
  {}
      
  void LArbysImageAna::configure(const PSet& cfg)
  {
    _track_vertex_estimate_algo_name = cfg.get<std::string>("TrackVertexEstimateAlgoName");
  }
  
  void LArbysImageAna::ClearVertex() {
    _vtx2d_x_v.clear();
    _vtx2d_y_v.clear();

    _circle_x_v.clear();
    _circle_y_v.clear();

    _vtx2d_x_v.resize(3);
    _vtx2d_y_v.resize(3);

    _circle_x_v.resize(3);
    _circle_y_v.resize(3);

    _circle_xs_v.resize(3);
  }

  void LArbysImageAna::initialize()
  {
    
    _event_tree = new TTree("EventTree","");

    _event_tree->Branch("run"    ,&_run    , "run/i");
    _event_tree->Branch("subrun" ,&_subrun , "subrun/i");
    _event_tree->Branch("event"  ,&_event  , "event/i");
    _event_tree->Branch("entry"  ,&_entry  , "entry/i");
    
    _vtx3d_tree = new TTree("Vtx3DTree","");

    _vtx3d_tree->Branch("run"    ,&_run    , "run/i");
    _vtx3d_tree->Branch("subrun" ,&_subrun , "subrun/i");
    _vtx3d_tree->Branch("event"  ,&_event  , "event/i");

    _vtx3d_tree->Branch("vtx3d_id", &_vtx3d_id, "vtx3d_id/i");
    _vtx3d_tree->Branch("vtx3d_type", &_vtx3d_type, "vtx3d_type/i");
    
    _vtx3d_tree->Branch("vtx3d_x", &_vtx3d_x, "vtx3d_x/D"  );
    _vtx3d_tree->Branch("vtx3d_y", &_vtx3d_y, "vtx3d_y/D"  );
    _vtx3d_tree->Branch("vtx3d_z", &_vtx3d_z, "vtx3d_z/D"  );

    _vtx3d_tree->Branch("vtx2d_x_v", &_vtx2d_x_v );
    _vtx3d_tree->Branch("vtx2d_y_v", &_vtx2d_y_v );

    _vtx3d_tree->Branch("circle_vtx_x_v",&_circle_x_v);
    _vtx3d_tree->Branch("circle_vtx_y_v",&_circle_y_v);
    _vtx3d_tree->Branch("circle_vtx_xs_v",&_circle_xs_v);

    _vtx3d_tree->Branch("vtx3d_n_planes",&_vtx3d_n_planes,"vtx3d_n_planes/i");
  }

  bool LArbysImageAna::process(IOManager& mgr)
  {
        
    LARCV_DEBUG() << "process" << std::endl;

    /// Unique event keys
    const auto& event_id = mgr.event_id();
    _run    = (uint) event_id.run();
    _subrun = (uint) event_id.subrun();
    _event  = (uint) event_id.event();
    _entry =  (uint) mgr.current_entry();
      
    const auto& dm  = _mgr_ptr->DataManager();    

    /// Refine2D data
    const auto trkvtxest_data = (larocv::data::VertexEstimateData*)dm.Data( dm.ID(_track_vertex_estimate_algo_name) );
      
    auto& vtx_cluster_v=  trkvtxest_data->get_vertex();
      
    _n_vtx3d = (uint) vtx_cluster_v.size();
    
    for(uint vtx_id=0;vtx_id<_n_vtx3d;++vtx_id) { 
	
      // clear vertex
      ClearVertex();
	
      // set the vertex ID
      _vtx3d_id=vtx_id;
	
      // get this 3D vertex
      const auto& vtx3d = vtx_cluster_v[vtx_id];
	
      // set the vertex type
      _vtx3d_type = (uint) trkvtxest_data->get_type(vtx_id);
	
      _vtx3d_x = vtx3d.x;
      _vtx3d_y = vtx3d.y;
      _vtx3d_z = vtx3d.z;
      
      _vtx3d_n_planes = (uint) vtx3d.num_planes;

      for(uint plane_id=0; plane_id<3;  ++plane_id) {

	if (_vtx3d_type < 2) {
	  const auto& circle_vtx   = trkvtxest_data->get_circle_vertex(vtx_id,plane_id);
	  const auto& circle_vtx_c = circle_vtx.center;
	  auto& circle_x  = _circle_x_v [plane_id];
	  auto& circle_y  = _circle_y_v [plane_id];
	  auto& circle_xs = _circle_xs_v[plane_id];
	  circle_x = circle_vtx_c.x;
	  circle_y = circle_vtx_c.y;
	  circle_xs = (uint) circle_vtx.xs_v.size();
	}
	
	auto& vtx2d_x = _vtx2d_x_v[plane_id];
	auto& vtx2d_y = _vtx2d_y_v[plane_id];
	  
	vtx2d_x = vtx3d.vtx2d_v[plane_id].pt.x;
	vtx2d_y = vtx3d.vtx2d_v[plane_id].pt.y;
	  
      } // end plane
      _vtx3d_tree->Fill();

    } // end loop over vtx
    
    _event_tree->Fill();
    
    return true;
  }

  void LArbysImageAna::finalize()
  {
    _event_tree->Write();
    _vtx3d_tree->Write();
  }

}
#endif

