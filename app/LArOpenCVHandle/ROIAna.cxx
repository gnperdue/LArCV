#ifndef __ROIANA_CXX__
#define __ROIANA_CXX__

#include "ROIAna.h"
#include <numeric>
#include <array>

namespace larcv {

  static ROIAnaProcessFactory __global_ROIAnaProcessFactory__;

  ROIAna::ROIAna(const std::string name)
    : ProcessBase(name), _roi_tree(nullptr)
  { clear(); }
    
  void ROIAna::configure(const PSet& cfg)
  {
    _roi_producer = cfg.get<std::string>("ROIProducer");
    _img_producer = cfg.get<std::string>("ImageProducer");
  }

  void ROIAna::initialize()
  {
    clear();
    _roi_tree = new TTree("ROITree","ROITree");

    _roi_tree->Branch("run",&_run,"run/I");
    _roi_tree->Branch("subrun",&_subrun,"subrun/I");
    _roi_tree->Branch("event",&_event,"event/I");
    _roi_tree->Branch("entry",&_entry,"entry/I");
      
    _roi_tree->Branch("nroi",&_nroi,"nroi/I");

    _roi_tree->Branch("area_exclusive0",&_area_exclusive0,"area_exclusive0/F");
    _roi_tree->Branch("area_exclusive1",&_area_exclusive1,"area_exclusive1/F");
    _roi_tree->Branch("area_exclusive2",&_area_exclusive2,"area_exclusive2/F");

    _roi_tree->Branch("area_inclusive0",&_area_inclusive0,"area_inclusive0/F");
    _roi_tree->Branch("area_inclusive1",&_area_inclusive1,"area_inclusive1/F");
    _roi_tree->Branch("area_inclusive2",&_area_inclusive2,"area_inclusive2/F");

    _roi_tree->Branch("area_union0",&_union_area0,"area_union0/F");
    _roi_tree->Branch("area_union1",&_union_area1,"area_union1/F");
    _roi_tree->Branch("area_union2",&_union_area2,"area_union2/F");

    _roi_tree->Branch("area_image0",&_area_image0,"area_image0/F");
    _roi_tree->Branch("area_image1",&_area_image1,"area_image1/F");
    _roi_tree->Branch("area_image2",&_area_image2,"area_image2/F");
  }

  bool ROIAna::process(IOManager& mgr)
  {

    EventROI *ev_roi = nullptr;
    if (!_roi_producer.empty()) {
      ev_roi = (EventROI*)mgr.get_data(kProductROI,_roi_producer);
      if (!ev_roi) {
	LARCV_CRITICAL() << "Requested ROI producer " << _roi_producer
			 << " could not be found" << std::endl;
	throw larbys();
      }
    }

    _run    = (int) ev_roi->run();
    _subrun = (int) ev_roi->subrun();
    _event  = (int) ev_roi->event();
    _entry  = (int) mgr.current_entry();
    
    EventImage2D *ev_img = nullptr;
    if (!_img_producer.empty()) {
      ev_img = (EventImage2D*)mgr.get_data(kProductImage2D,_img_producer);
      if (!ev_img) {
	LARCV_CRITICAL() << "Requested Image2D producer " << _img_producer
			 << " could not be found" << std::endl;
	throw larbys();
      }
    }


    double pixel_height = ev_img->Image2DArray().front().meta().pixel_height();
    double pixel_width  = ev_img->Image2DArray().front().meta().pixel_width();

    std::array<float,3> area_exclusive_v;
    std::array<float,3> area_inclusive_v;
    std::array<float,3> area_union_v;
    std::array<float,3> area_image_v;
    
    for(auto& v : area_exclusive_v) v=0;
    for(auto& v : area_inclusive_v) v=0;
    for(auto& v : area_union_v) v=0;
    for(auto& v : area_image_v) v=0;
    
    _nroi = ev_roi->ROIArray().size();

    std::vector<std::vector<const ImageMeta*> > meta_vv;
    meta_vv.resize(3);
    for(auto& meta_v : meta_vv) meta_v.reserve(_nroi);
    
    for(const auto& roi : ev_roi->ROIArray() ) {
      for(size_t plane=0; plane < 3; ++plane) {
	meta_vv[plane].emplace_back(&(roi.BB(plane)));
      }
    }

    std::array<ImageMeta,3> union_meta_v;
    
    for(size_t plane=0; plane<3; ++plane) {
      const auto& meta_v = meta_vv[plane];

      auto& area_exclusive    = area_exclusive_v.at(plane);
      auto& area_inclusive    = area_inclusive_v.at(plane);
      auto& area_union        = area_union_v.at(plane);
      auto& union_meta        = union_meta_v.at(plane);
      auto& area_image        = area_image_v.at(plane);
	
      //
      // sum the area
      //
      for(auto meta : meta_v) 
	area_inclusive += area(meta);

      //
      // calculate the union size
      //
      bool first = true;
      for(auto meta : meta_v) {
	if (first) {
	  union_meta = *meta;
	  first = false;
	  continue;
	}
	union_meta = union_meta.inclusive(*meta);
      }
      area_union = area(union_meta);

      //
      // calculate exclusive area
      //
      double width  = pixel_width  * union_meta.cols();
      double height = pixel_height * union_meta.rows();
      union_meta = ImageMeta(width,height,
			     union_meta.rows(),union_meta.cols(),
			     union_meta.tl().x,union_meta.tl().y,
			     plane);

      Image2D mask(union_meta);
      mask.paint(0.0);

      for(auto meta : meta_v) {
	ImageMeta bb(pixel_width*(meta->cols()),pixel_height*(meta->rows()),
		     meta->rows(),meta->cols(),
		     meta->tl().x,meta->tl().y,
		     plane);
	
	Image2D img_bb(bb);
	img_bb.paint(1.0);
	mask.overlay(img_bb,Image2D::kOverWrite);
      }
      const auto& mask_v = mask.as_vector();
      area_exclusive = std::accumulate(std::begin(mask_v),std::end(mask_v),0.0);

      //
      // calculate the image size
      //
      area_image = area(ev_img->Image2DArray().at(plane).meta());
      
    }

    
    for(size_t plane=0; plane<3; ++plane) {
      if(plane==0) {
	_area_exclusive0 = area_exclusive_v[plane];
	_area_inclusive0 = area_inclusive_v[plane];
	_union_area0     = area_union_v[plane];
	_area_image0     = area_image_v[plane];
      }
      if(plane==1) {
	_area_exclusive1 = area_exclusive_v[plane];
	_area_inclusive1 = area_inclusive_v[plane];
	_union_area1     = area_union_v[plane];
	_area_image1     = area_image_v[plane];
      }
      if(plane==2) {
	_area_exclusive2 = area_exclusive_v[plane];
	_area_inclusive2 = area_inclusive_v[plane];
	_union_area2     = area_union_v[plane];
	_area_image2     = area_image_v[plane];
      }
    }

    _roi_tree->Fill();
    return true;
  }

  void ROIAna::finalize()
  {
    _roi_tree->Write();
  }

  float ROIAna::area(const ImageMeta& meta) const {
    return (float) meta.rows() * (float) meta.cols();
  }

  float ROIAna::area(const ImageMeta* meta) const {
    return area(*meta);
  }

  void ROIAna::clear() {
    _nroi = 0;
    
    _area_exclusive0 = 0.0;
    _area_exclusive1 = 0.0;
    _area_exclusive2 = 0.0;

    _area_inclusive0 = 0.0;
    _area_inclusive1 = 0.0;
    _area_inclusive2 = 0.0;

    _union_area0 = 0.0;
    _union_area1 = 0.0;
    _union_area2 = 0.0;

    _area_image0 = 0.0;
    _area_image1 = 0.0;
    _area_image2 = 0.0;
  }
  
}
#endif

