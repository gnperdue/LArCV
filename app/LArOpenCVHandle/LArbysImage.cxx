#ifndef __LARBYSIMAGE_CXX__
#define __LARBYSIMAGE_CXX__

#include "LArbysImage.h"
#include "Base/ConfigManager.h"
#include "DataFormat/EventImage2D.h"
#include "DataFormat/EventROI.h"
#include "DataFormat/EventPGraph.h"
#include "DataFormat/EventPixel2D.h"
#include "LArOpenCV/ImageCluster/AlgoFunction/Contour2DAnalysis.h"
#include "LArOpenCV/ImageCluster/AlgoFunction/ImagePatchAnalysis.h"
#include "LArbysUtils.h"
#include "LArOpenCV/ImageCluster/AlgoData/Vertex.h"
#include "LArOpenCV/ImageCluster/AlgoData/ParticleCluster.h"
#include <array>

namespace larcv {

  static LArbysImageProcessFactory __global_LArbysImageProcessFactory__;

  LArbysImage::LArbysImage(const std::string name)
    : ProcessBase(name),
      _PreProcessor(),
      _LArbysImageMaker()
  {}
      
  void LArbysImage::configure(const PSet& cfg)
  {
    _adc_producer         = cfg.get<std::string>("ADCImageProducer");
    _track_producer       = cfg.get<std::string>("TrackImageProducer","");
    _shower_producer      = cfg.get<std::string>("ShowerImageProducer","");
    _thrumu_producer      = cfg.get<std::string>("ThruMuImageProducer","");
    _stopmu_producer      = cfg.get<std::string>("StopMuImageProducer","");
    _roi_producer         = cfg.get<std::string>("ROIProducer","");
    _output_producer      = cfg.get<std::string>("OutputImageProducer","");

    _mask_thrumu_pixels = cfg.get<bool>("MaskThruMu",false);
    _mask_stopmu_pixels = cfg.get<bool>("MaskStopMu",false);

    _LArbysImageMaker.Configure(cfg.get<larcv::PSet>("LArbysImageMaker"));

    _preprocess = cfg.get<bool>("PreProcess");
    if (_preprocess) {
      LARCV_INFO() << "Preprocessing image" << std::endl;
      _PreProcessor.Configure(cfg.get<larcv::PSet>("PreProcessor"));
    }
    
    _process_count = 0;
    _process_time_image_extraction = 0;
    _process_time_analyze = 0;
    _process_time_cluster_storage = 0;
    
    ::fcllite::PSet copy_cfg(_alg_mgr.Name(),cfg.get_pset(_alg_mgr.Name()).data_string());
    _alg_mgr.Configure(copy_cfg.get_pset(_alg_mgr.Name()));

    _union_roi = cfg.get<bool>("UnionROI",false);

    _vertex_algo_name = cfg.get<std::string>("VertexAlgoName","");
    _par_algo_name    = cfg.get<std::string>("ParticleAlgoName","");

    const auto& data_man = _alg_mgr.DataManager();
      
    _vertex_algo_id = larocv::kINVALID_ALGO_ID;
    if (!_vertex_algo_name.empty()) {
      _vertex_algo_id = data_man.ID(_vertex_algo_name);
      if (_vertex_algo_id == larocv::kINVALID_ALGO_ID)
	throw larbys("Specified invalid vertex algorithm");
    }

    _par_algo_id = larocv::kINVALID_ALGO_ID;
    if (!_par_algo_name.empty()) {
      _par_algo_id = data_man.ID(_par_algo_name);
      if (_par_algo_id == larocv::kINVALID_ALGO_ID)
	throw larbys("Specified invalid particle algorithm");
    }

    _vertex_algo_vertex_offset = cfg.get<size_t>("VertexAlgoVertexOffset",0);
    _par_algo_par_offset       = cfg.get<size_t>("ParticleAlgoParticleOffset",0);
  }
  
  void LArbysImage::initialize()
  {
    _thrumu_image_v.clear();
    _stopmu_image_v.clear();
  }

  void LArbysImage::get_rsee(IOManager& mgr,std::string producer,uint& run, uint& subrun, uint& event, uint& entry) {
    auto ev_image = (EventImage2D*)(mgr.get_data(kProductImage2D,producer));
    run    = (uint) ev_image->run();
    subrun = (uint) ev_image->subrun();
    event  = (uint) ev_image->event();
    entry  = (uint) mgr.current_entry();
  }
  
  
  const std::vector<larcv::Image2D>& LArbysImage::get_image2d(IOManager& mgr, std::string producer) {
    
    if(!producer.empty()) {
      LARCV_DEBUG() << "Extracting " << producer << " Image\n" << std::endl;
      auto ev_image = (EventImage2D*)(mgr.get_data(kProductImage2D,producer));
      if(!ev_image) {
	LARCV_CRITICAL() << "Image by producer " << producer << " not found..." << std::endl;
	throw larbys();
      }
      return ev_image->Image2DArray();
    }
    LARCV_DEBUG() << "... this producer empty" << std::endl;
    return _empty_image_v;
  }

  void LArbysImage::construct_cosmic_image(IOManager& mgr, std::string producer,
					   const std::vector<larcv::Image2D>& adc_image_v,
					   std::vector<larcv::Image2D>& mu_image_v) {
    LARCV_DEBUG() << "Constructing " << producer << " Pixel2D => Image2D" << std::endl;
    if(!producer.empty()) {
      auto ev_pixel2d = (EventPixel2D*)(mgr.get_data(kProductPixel2D,producer));
      if(!ev_pixel2d) {
	LARCV_CRITICAL() << "Pixel2D by producer " << producer << " not found..." << std::endl;
	throw larbys();
      }
      LARCV_DEBUG() << "Using Pixel2D producer " << producer << std::endl;
      auto const& pixel2d_m = ev_pixel2d->Pixel2DClusterArray();
      for(size_t img_idx=0; img_idx<adc_image_v.size(); ++img_idx) {
	auto const& meta = adc_image_v[img_idx].meta();
	if(mu_image_v.size() <= img_idx)
	  mu_image_v.emplace_back(larcv::Image2D(meta));
	if(mu_image_v[img_idx].meta() != meta)
	  mu_image_v[img_idx] = larcv::Image2D(meta);
	auto& mu_image = mu_image_v[img_idx];
	mu_image.paint(0);
	auto itr = pixel2d_m.find(img_idx);
	if(itr == pixel2d_m.end()) {
	  LARCV_DEBUG() << "No Pixel2D found for plane " << img_idx << std::endl;
	  continue;
	}
	else{
	  size_t npx_x = meta.cols();
	  size_t npx_y = meta.rows();
	  for(auto const& pixel_cluster : (*itr).second) {
	    for(auto const& pixel : pixel_cluster) {
	      if(pixel.X() >= npx_x || pixel.Y() >= npx_y) {
		LARCV_WARNING() << "Ignoring cosmic pixel (row,col) = ("
				<< pixel.Y() << "," << pixel.X() << ")"
				<< " as it is out of bounds (ncol=" << npx_x << ", nrow=" << npx_y << ")" << std::endl;
		continue;
	      }
	      mu_image.set_pixel( (pixel.X() * meta.rows() + pixel.Y()), 100 );
	    }
	  }
	}
      }
    }
  }
  
  bool LArbysImage::process(IOManager& mgr)
  {
    LARCV_DEBUG() << "Process index " << mgr.current_entry() << std::endl;
    uint run,subrun,event,entry;
    get_rsee(mgr,_adc_producer,run,subrun,event,entry);
    
    _alg_mgr.SetRSEE(run,subrun,event,entry);
    
    bool status = true;

    if(_roi_producer.empty()) {
      size_t pidx = 0;          
      auto const& adc_image_v    = get_image2d(mgr,_adc_producer);
      auto const& track_image_v  = get_image2d(mgr,_track_producer);
      auto const& shower_image_v = get_image2d(mgr,_shower_producer);
      construct_cosmic_image(mgr, _thrumu_producer, adc_image_v, _thrumu_image_v);
      construct_cosmic_image(mgr, _stopmu_producer, adc_image_v, _stopmu_image_v);
      auto const& thrumu_image_v = _thrumu_image_v;
      auto const& stopmu_image_v = _stopmu_image_v;
      
      assert(adc_image_v.size());
      assert(track_image_v.empty()  || adc_image_v.size() == track_image_v.size());
      assert(shower_image_v.empty() || adc_image_v.size() == shower_image_v.size());
      assert(thrumu_image_v.empty() || adc_image_v.size() == thrumu_image_v.size());
      assert(stopmu_image_v.empty() || adc_image_v.size() == stopmu_image_v.size());

      bool mask_thrumu = _mask_thrumu_pixels && !thrumu_image_v.empty();
      bool mask_stopmu = _mask_stopmu_pixels && !stopmu_image_v.empty();

      if(!mask_thrumu && !mask_stopmu) {
	LARCV_DEBUG() << "Reconstruct no mask thrumu and no mask stopmu" << std::endl;
	status = Reconstruct(adc_image_v,
			     track_image_v,shower_image_v,
			     thrumu_image_v,stopmu_image_v);
	status = status && StoreParticles(mgr,adc_image_v,pidx);
	
      } //Don't mask stop mu or thru mu
      else { 
	
	auto copy_adc_image_v    = adc_image_v;
	auto copy_track_image_v  = track_image_v;
	auto copy_shower_image_v = shower_image_v;
	LARCV_DEBUG() << "ADC image size " << adc_image_v.size() << std::endl;
	for(size_t plane=0; plane<adc_image_v.size(); ++plane) {
	  
	  if(mask_thrumu) {
	    LARCV_DEBUG() << "Masking thrumu plane " << plane << std::endl;
	    mask_image(copy_adc_image_v[plane], thrumu_image_v[plane]);
	    if(!copy_track_image_v.empty() ) mask_image(copy_track_image_v[plane],  thrumu_image_v[plane]);
	    if(!copy_shower_image_v.empty()) mask_image(copy_shower_image_v[plane], thrumu_image_v[plane]);
	  }
	  
	  if(mask_stopmu) {
	    LARCV_DEBUG() << "Masking stopmu plane " << plane << std::endl;
	    mask_image(copy_adc_image_v[plane], stopmu_image_v[plane]);
	    if(!copy_track_image_v.empty() ) mask_image(copy_track_image_v[plane],  stopmu_image_v[plane]);
	    if(!copy_shower_image_v.empty()) mask_image(copy_shower_image_v[plane], stopmu_image_v[plane]);
	  }

	}

	status = Reconstruct(copy_adc_image_v,
			     copy_track_image_v,copy_shower_image_v,
			     thrumu_image_v, stopmu_image_v);
	status = status && StoreParticles(mgr,copy_adc_image_v,pidx);
      }
    } //ROI exists
    else{

      size_t pidx = 0;
      auto const& adc_image_v    = get_image2d(mgr,_adc_producer);
      auto const& track_image_v  = get_image2d(mgr,_track_producer);
      auto const& shower_image_v = get_image2d(mgr,_shower_producer);
      construct_cosmic_image(mgr, _thrumu_producer, adc_image_v, _thrumu_image_v);
      construct_cosmic_image(mgr, _stopmu_producer, adc_image_v, _stopmu_image_v);
      auto const& thrumu_image_v = _thrumu_image_v;
      auto const& stopmu_image_v = _stopmu_image_v;

      auto pixel_height = adc_image_v.front().meta().pixel_height();
      auto pixel_width  = adc_image_v.front().meta().pixel_width();
      
      assert(adc_image_v.size());
      assert(track_image_v.empty()  || adc_image_v.size() == track_image_v.size());
      assert(shower_image_v.empty() || adc_image_v.size() == shower_image_v.size());
      assert(thrumu_image_v.empty() || adc_image_v.size() == thrumu_image_v.size());
      assert(stopmu_image_v.empty() || adc_image_v.size() == stopmu_image_v.size());

      LARCV_DEBUG() << "Extracting " << _roi_producer << " ROI\n" << std::endl;
      if( !(mgr.get_data(kProductROI,_roi_producer)) ) {
	LARCV_CRITICAL() << "ROI by producer " << _roi_producer << " not found..." << std::endl;
	throw larbys();
      }
      auto const& roi_v = ((EventROI*)(mgr.get_data(kProductROI,_roi_producer)))->ROIArray();

      if(_union_roi) {

	std::vector<larcv::Image2D> union_adc_image_v(3);
	std::vector<larcv::Image2D> union_track_image_v(3);
	std::vector<larcv::Image2D> union_shower_image_v(3);
	std::vector<larcv::Image2D> union_thrumu_image_v(3);
	std::vector<larcv::Image2D> union_stopmu_image_v(3);

	std::vector<larcv::ImageMeta> union_bb_v(3);

	for(size_t plane=0; plane<3; ++plane) 
	  union_bb_v[plane] = roi_v.front().BB(plane);
       
	bool first = false;
	for(auto const& roi : roi_v) {
	  
	  if (!first) { first = true; continue; }

	  auto const& bb_v = roi.BB();
	  assert(bb_v.size() == adc_image_v.size());

	  for(size_t plane=0; plane<bb_v.size(); ++plane) {
	    auto const& bb = bb_v[plane];
	    auto& union_bb = union_bb_v[plane];
	    union_bb = union_bb.inclusive(bb);
	    double width  = pixel_width  * union_bb.cols();
	    double height = pixel_height * union_bb.rows();
	    union_bb = ImageMeta(width,height,
				 union_bb.rows(),union_bb.cols(),
				 union_bb.tl().x,union_bb.tl().y,
				 plane);
	  }
	}

	std::vector<Image2D> mask_v(3);
	for(size_t plane=0; plane<3; ++plane) {
	  const auto& union_bb = union_bb_v[plane];
	  mask_v[plane] = Image2D(union_bb);
	  mask_v[plane].paint(0.0);
	}
	
	for(auto const& roi : roi_v) {
	  auto const& bb_v = roi.BB();
	  for(size_t plane=0; plane<bb_v.size(); ++plane) {
	    auto bb = bb_v[plane];
	    auto& mask = mask_v[plane];
	    bb = ImageMeta(pixel_width*bb.cols(),pixel_height*bb.rows(),
			   bb.rows(),bb.cols(),
			   bb.tl().x,bb.tl().y,
			   plane);
	    
	    auto img_bb = Image2D(bb);
	    img_bb.paint(1.0);
	    mask.overlay(img_bb,Image2D::kOverWrite);
	  }
	}

	for(size_t plane=0; plane<3; ++plane) {

	  auto& union_adc_image  = union_adc_image_v[plane];   
	  auto& union_track_image = union_track_image_v[plane]; 
	  auto& union_shower_image = union_shower_image_v[plane];
	  auto& union_thrumu_image = union_thrumu_image_v[plane];
	  auto& union_stopmu_image = union_stopmu_image_v[plane];	

	  const auto& mask_meta = mask_v[plane].meta();
	  
	  union_adc_image = mask_v[plane];
	  union_adc_image.eltwise(adc_image_v[plane].crop(mask_meta));

	  if(!track_image_v.empty()) {
	    union_track_image = mask_v[plane];
	    union_track_image.eltwise(track_image_v[plane].crop(mask_meta));
	  }

	  if(!shower_image_v.empty()) {
	    union_shower_image = mask_v[plane];
	    union_shower_image.eltwise(shower_image_v[plane].crop(mask_meta));
	  }

	  if(!thrumu_image_v.empty()) {
	    union_thrumu_image = mask_v[plane];
	    union_thrumu_image.eltwise(thrumu_image_v[plane].crop(mask_meta));
	  }
	  
	  if(!stopmu_image_v.empty()) { 
	    union_stopmu_image = mask_v[plane];
	    union_stopmu_image.eltwise(stopmu_image_v[plane].crop(mask_meta));
	  }
	  
	  if(!union_thrumu_image_v.empty() && _mask_thrumu_pixels) {
	    LARCV_DEBUG() << "Masking thrumu plane " << plane << std::endl;
	    mask_image(union_adc_image, union_thrumu_image);
	    if(!union_track_image_v.empty() ) mask_image(union_track_image,  union_thrumu_image);
	    if(!union_shower_image_v.empty()) mask_image(union_shower_image, union_thrumu_image);
	  }
	  
	  if(!union_stopmu_image_v.empty() && _mask_stopmu_pixels) {
	    LARCV_DEBUG() << "Masking stopmu plane " << plane << std::endl;
	    mask_image(union_adc_image, union_stopmu_image);
	    if(!union_track_image_v.empty() ) mask_image(union_track_image,  union_stopmu_image);
	    if(!union_shower_image_v.empty()) mask_image(union_shower_image, union_stopmu_image);
	  }
	  
	}
	
	
	status = status && Reconstruct(union_adc_image_v,
				       union_track_image_v, union_shower_image_v,
				       union_thrumu_image_v, union_stopmu_image_v);
	
	status = status && StoreParticles(mgr,union_adc_image_v,pidx);
	
      } //end union
      else { //give ROI 1 by 1
      
	for(auto const& roi : roi_v) {

	  auto const& bb_v = roi.BB();
	  assert(bb_v.size() == adc_image_v.size());

	  std::vector<larcv::Image2D> crop_adc_image_v;
	  std::vector<larcv::Image2D> crop_track_image_v;
	  std::vector<larcv::Image2D> crop_shower_image_v;
	  std::vector<larcv::Image2D> crop_thrumu_image_v;
	  std::vector<larcv::Image2D> crop_stopmu_image_v;

	  for(size_t plane=0; plane<bb_v.size(); ++plane) {

	    auto const& bb           = bb_v[plane];
	    auto const& adc_image    = adc_image_v[plane];
	  
	    crop_adc_image_v.emplace_back(adc_image.crop(bb));

	    if(!track_image_v.empty()) {
	      auto const& track_image  = track_image_v[plane];
	      crop_track_image_v.emplace_back(track_image.crop(bb));
	    }

	    if(!shower_image_v.empty()) {
	      auto const& shower_image = shower_image_v[plane];
	      crop_shower_image_v.emplace_back(shower_image.crop(bb));
	    }

	    if(!thrumu_image_v.empty()) {
	      auto const& thrumu_image = thrumu_image_v[plane];
	      crop_thrumu_image_v.emplace_back(thrumu_image.crop(bb));
	    }

	    if(!stopmu_image_v.empty()) {
	      auto const& stopmu_image = stopmu_image_v[plane];
	      crop_stopmu_image_v.emplace_back(stopmu_image.crop(bb));
	    }

	    if(!crop_thrumu_image_v.empty() && _mask_thrumu_pixels) {
	      LARCV_DEBUG() << "Masking thrumu plane " << plane << std::endl;
	      mask_image(crop_adc_image_v[plane], crop_thrumu_image_v[plane]);
	      if(!crop_track_image_v.empty() ) mask_image(crop_track_image_v[plane],  crop_thrumu_image_v[plane]);
	      if(!crop_shower_image_v.empty()) mask_image(crop_shower_image_v[plane], crop_thrumu_image_v[plane]);
	    }

	    if(!crop_stopmu_image_v.empty() && _mask_stopmu_pixels) {
	      LARCV_DEBUG() << "Masking stopmu plane " << plane << std::endl;
	      mask_image(crop_adc_image_v[plane], crop_stopmu_image_v[plane]);
	      if(!crop_track_image_v.empty() ) mask_image(crop_track_image_v[plane],  crop_stopmu_image_v[plane]);
	      if(!crop_shower_image_v.empty()) mask_image(crop_shower_image_v[plane], crop_stopmu_image_v[plane]);
	    }

	  }
	
	  status = status && Reconstruct(crop_adc_image_v,
					 crop_track_image_v, crop_shower_image_v,
					 crop_thrumu_image_v, crop_stopmu_image_v);
	
	  status = status && StoreParticles(mgr,crop_adc_image_v,pidx);
	} // end loop over ROI
      } // end ROI exists
    } // end image fed to reco

    LARCV_DEBUG() << "return " << status << std::endl;
    return status;
  }

  bool LArbysImage::StoreParticles(IOManager& iom,
				   const std::vector<Image2D>& adc_image_v,
				   size_t& pidx) {

    // nothing to be done
    if (_vertex_algo_id == larocv::kINVALID_ALGO_ID) {
      LARCV_INFO() << "Nothing to be done..." << std::endl;
      return true;
    }
    
    const auto& adc_cvimg_orig_v = _alg_mgr.InputImages(larocv::ImageSetID_t::kImageSetWire);
    static std::vector<cv::Mat> adc_cvimg_v;
    adc_cvimg_v.clear();
    adc_cvimg_v.resize(3);

    auto event_pgraph        = (EventPGraph*)  iom.get_data(kProductPGraph,_output_producer);
    auto event_ctor_pixel    = (EventPixel2D*) iom.get_data(kProductPixel2D,_output_producer+"_ctor");
    auto event_img_pixel     = (EventPixel2D*) iom.get_data(kProductPixel2D,_output_producer+"_img");
    
    const auto& data_mgr = _alg_mgr.DataManager();
    const auto& ass_man  = data_mgr.AssManager();
    
    const auto vtx3d_array = (larocv::data::Vertex3DArray*) data_mgr.Data(_vertex_algo_id, _vertex_algo_vertex_offset);
    const auto& vtx3d_v    = vtx3d_array->as_vector();
    
    const auto par_array = (larocv::data::ParticleArray*) data_mgr.Data(_par_algo_id,_par_algo_par_offset);
    const auto& par_v     = par_array->as_vector();

    auto n_reco_vtx = vtx3d_v.size();
    LARCV_DEBUG() << "Matching... " << n_reco_vtx << " vertices" << std::endl;
    for(size_t vtxid=0; vtxid< n_reco_vtx; ++vtxid) {
      
      const auto& vtx3d = vtx3d_v[vtxid];

      LARCV_DEBUG() << vtxid << ") @ (x,y,z) : ("<<vtx3d.x<<","<<vtx3d.y<<","<<vtx3d.z<<")"<<std::endl;

      for(size_t plane=0; plane<3; ++plane)
	adc_cvimg_v[plane] = adc_cvimg_orig_v[plane].clone();

      auto par_id_v = ass_man.GetManyAss(vtx3d,par_array->ID());
      if (par_id_v.empty()) {
	LARCV_DEBUG() << "No associated particles to vertex " << vtxid << std::endl;
	continue;
      }

      PGraph pgraph;
      for(const auto& par_id : par_id_v) {
	
	const auto& par = par_v.at(par_id);
	
	// New ROI for this particle
	ROI proi;
	
	// Store the vertex
	proi.Position(vtx3d.x,vtx3d.y,vtx3d.z,kINVALID_DOUBLE);

	// Store the type
	if      (par.type==larocv::data::ParticleType_t::kTrack)   proi.Shape(kShapeTrack);
	else if (par.type==larocv::data::ParticleType_t::kShower)  proi.Shape(kShapeShower);
	  
	// Push the ROI into the PGraph
	LARCV_DEBUG() << " @ pg array index " << pidx << std::endl;

	for(size_t plane=0; plane<3; ++plane) 
	  proi.AppendBB(adc_image_v[plane].meta());
	
	pgraph.Emplace(std::move(proi),pidx);
	pidx++;

	// @ Each plane, store pixels and contour per matched particle
	for(size_t plane=0; plane<3; ++plane) {
	  const auto& pmeta = adc_image_v[plane].meta();
	    
	  std::vector<Pixel2D> pixel_v, ctor_v;
	    
	  const auto& pcluster = par._par_v[plane];
	  const auto& pctor = pcluster._ctor;
	  
	  const auto& img2d = adc_image_v[plane];
	  auto& cvimg = adc_cvimg_v[plane];
	  
	  if(!pctor.empty()) {
	    auto masked = larocv::MaskImage(cvimg,pctor,0,false);
	    auto par_pixel_v = larocv::FindNonZero(masked);
	    pixel_v.reserve(par_pixel_v.size());
	    cvimg = larocv::MaskImage(cvimg,pctor,0,true);
	    
	    // Store Image2D pixel values
	    pixel_v.reserve(par_pixel_v.size());
	    for (const auto& px : par_pixel_v) {
	      auto col  = cvimg.cols - px.x - 1;
	      auto row  = px.y;
	      auto gray = img2d.pixel(col,row);
	      pixel_v.emplace_back(col,row);
	      pixel_v.back().Intensity(gray);
	    }

	    // Store contour
	    ctor_v.reserve(pctor.size());
	    for(const auto& pt : pctor)  {
	      auto col  = cvimg.cols - pt.x - 1;
	      auto row  = pt.y;
	      auto gray = 1.0;
	      ctor_v.emplace_back(row,col);
	      ctor_v.back().Intensity(gray);
	    }
	  }
	  
	  Pixel2DCluster pixcluster(std::move(pixel_v));
	  event_img_pixel->Emplace(plane,std::move(pixcluster),pmeta);

	  Pixel2DCluster pixctor(std::move(ctor_v));
	  event_ctor_pixel->Emplace(plane,std::move(pixctor),pmeta);

	} // end this plane
      } // end this particle
      
      event_pgraph->Emplace(std::move(pgraph));
    } // end vertex

    LARCV_DEBUG() << "Event pgraph size " << event_pgraph->PGraphArray().size() << std::endl;

    return true;
  }
  
  bool LArbysImage::Reconstruct(const std::vector<larcv::Image2D>& adc_image_v,
				const std::vector<larcv::Image2D>& track_image_v,
				const std::vector<larcv::Image2D>& shower_image_v,
				const std::vector<larcv::Image2D>& thrumu_image_v,
				const std::vector<larcv::Image2D>& stopmu_image_v)
  {
    _adc_img_mgr.clear();
    _track_img_mgr.clear();
    _shower_img_mgr.clear();
    _thrumu_img_mgr.clear();
    _stopmu_img_mgr.clear();
    _alg_mgr.ClearData();
    
    larocv::Watch watch_all, watch_one;
    watch_all.Start();
    watch_one.Start();
    
    for(auto& img_data : _LArbysImageMaker.ExtractImage(adc_image_v)) {
      _adc_img_mgr.emplace_back(std::move(std::get<0>(img_data)),
				std::move(std::get<1>(img_data)));
    }

    if(!_track_producer.empty()) {
      for(auto& img_data : _LArbysImageMaker.ExtractImage(track_image_v))  { 
	_track_img_mgr.emplace_back(std::move(std::get<0>(img_data)),
				    std::move(std::get<1>(img_data)));
      }
    }
    
    if(!_shower_producer.empty()) {
      for(auto& img_data : _LArbysImageMaker.ExtractImage(shower_image_v)) {
	_shower_img_mgr.emplace_back(std::move(std::get<0>(img_data)),
				     std::move(std::get<1>(img_data)));
      }
    }

    if(!_stopmu_producer.empty()) { 
      for(auto& img_data : _LArbysImageMaker.ExtractImage(thrumu_image_v)) {
	_thrumu_img_mgr.emplace_back(std::move(std::get<0>(img_data)),
				     std::move(std::get<1>(img_data)));
      }
    }
    if(!_thrumu_producer.empty()) {
      for(auto& img_data : _LArbysImageMaker.ExtractImage(stopmu_image_v)) {
	_stopmu_img_mgr.emplace_back(std::move(std::get<0>(img_data)),
				     std::move(std::get<1>(img_data)));
      }
    }
    
    _process_time_image_extraction += watch_one.WallTime();

    for (size_t plane = 0; plane < _adc_img_mgr.size(); ++plane) {

      auto       & img  = _adc_img_mgr.img_at(plane);
      const auto & meta = _adc_img_mgr.meta_at(plane);
      const auto & roi  = _adc_img_mgr.roi_at(plane);

      if (!meta.num_pixel_row() || !meta.num_pixel_column()) continue;
      _alg_mgr.Add(img, meta, roi, larocv::ImageSetID_t::kImageSetWire);
    }

    for (size_t plane = 0; plane < _track_img_mgr.size(); ++plane) {
      
      auto       & img  = _track_img_mgr.img_at(plane);
      const auto & meta = _track_img_mgr.meta_at(plane);
      const auto & roi  = _track_img_mgr.roi_at(plane);

      if (!meta.num_pixel_row() || !meta.num_pixel_column()) continue;
      _alg_mgr.Add(img, meta, roi, larocv::ImageSetID_t::kImageSetTrack);
    }
    
    for (size_t plane = 0; plane < _shower_img_mgr.size(); ++plane) {

      auto       & img  = _shower_img_mgr.img_at(plane);
      const auto & meta = _shower_img_mgr.meta_at(plane);
      const auto & roi  = _shower_img_mgr.roi_at(plane);

      if (!meta.num_pixel_row() || !meta.num_pixel_column()) continue;
      _alg_mgr.Add(img, meta, roi, larocv::ImageSetID_t::kImageSetShower);
    }

    for (size_t plane = 0; plane < _thrumu_img_mgr.size(); ++plane) {

      auto       & img  = _thrumu_img_mgr.img_at(plane);
      const auto & meta = _thrumu_img_mgr.meta_at(plane);
      const auto & roi  = _thrumu_img_mgr.roi_at(plane);
      
      if (!meta.num_pixel_row() || !meta.num_pixel_column()) continue;
      _alg_mgr.Add(img, meta, roi, larocv::ImageSetID_t::kImageSetThruMu);
    }

    for (size_t plane = 0; plane < _stopmu_img_mgr.size(); ++plane) {

      auto       & img  = _stopmu_img_mgr.img_at(plane);
      const auto & meta = _stopmu_img_mgr.meta_at(plane);
      const auto & roi  = _stopmu_img_mgr.roi_at(plane);

      if (!meta.num_pixel_row() || !meta.num_pixel_column()) continue;
      _alg_mgr.Add(img, meta, roi, larocv::ImageSetID_t::kImageSetStopMu);
    }

    if (_preprocess) {
      //give a single plane @ a time to pre processor
      auto& adc_img_v= _alg_mgr.InputImages(larocv::ImageSetID_t::kImageSetWire);
      auto& trk_img_v= _alg_mgr.InputImages(larocv::ImageSetID_t::kImageSetTrack);
      auto& shr_img_v= _alg_mgr.InputImages(larocv::ImageSetID_t::kImageSetShower);
      auto nplanes = adc_img_v.size();
      for(size_t plane_id=0;plane_id<nplanes;++plane_id) {
	LARCV_DEBUG() << "Preprocess image set @ "<< " plane " << plane_id << std::endl;
	if (!_PreProcessor.PreProcess(adc_img_v[plane_id],trk_img_v[plane_id],shr_img_v[plane_id])) {
	  LARCV_CRITICAL() << "... could not be preprocessed, abort!" << std::endl;
	  throw larbys();
	}
      }
    }

    _alg_mgr.Process();

    watch_one.Start();
     
    _process_time_cluster_storage += watch_one.WallTime();

    _process_time_analyze += watch_all.WallTime();
    
    ++_process_count;
    
    return true;
  }

  void LArbysImage::finalize()
  {
    if ( has_ana_file() ) 
      _alg_mgr.Finalize(&(ana_file()));

  }
  
}
#endif
