#ifndef __LARBYSIMAGEEXTRACT_H__
#define __LARBYSIMAGEEXTRACT_H__

#include "Processor/ProcessBase.h"
#include "Processor/ProcessFactory.h"
#include "LArbysImageMaker.h"
#include "PreProcessor.h"

namespace larcv {

  class LArbysImageExtract : public ProcessBase {

  public:
    
    LArbysImageExtract(const std::string name="LArbysImageExtract");
    ~LArbysImageExtract(){}

    void configure(const PSet&);
    void initialize();
    bool process(IOManager& mgr);
    void finalize();

    const std::vector<cv::Mat>&
    ADCImages()
    { return _adc_mat_v; }

    const std::vector<larocv::ImageMeta>&
    ADCMetas()
    { return _adc_meta_v; }
    
    const cv::Mat&
    ADCImage(size_t planeid)
    { return _adc_mat_v[planeid]; }

    const larocv::ImageMeta&
    ADCMeta(size_t planeid)
    { return _adc_meta_v[planeid]; }
    
    const std::vector<cv::Mat>&
    TrackImages()
    { return _track_mat_v; }
    
    const cv::Mat&
    TrackImage(size_t planeid)
    { return _track_mat_v[planeid]; }
    
    const std::vector<cv::Mat>&
    ShowerImages()
    { return _shower_mat_v; }
    
    const cv::Mat&
    ShowerImage(size_t planeid)
    { return _shower_mat_v[planeid]; }

    const LArbysImageMaker&
    maker()
    { return _LArbysImageMaker; }

    const PreProcessor&
    pproc()
    { return _PreProcessor; }
    
  private:
    std::string _adc_producer;
    std::string _track_producer;
    std::string _shower_producer;
    std::string _thrumu_producer;
    std::string _stopmu_producer;
    
    LArbysImageMaker _LArbysImageMaker;
    PreProcessor _PreProcessor;
    

    // The images
    std::vector<cv::Mat> _adc_mat_v;
    std::vector<larocv::ImageMeta> _adc_meta_v;
    std::vector<cv::Mat> _track_mat_v;
    std::vector<cv::Mat> _shower_mat_v;


    EventImage2D _ev_adc;
    EventImage2D _ev_trk;
    EventImage2D _ev_shr;    

    EventPixel2D _ev_thrumu_pix;
    EventPixel2D _ev_stopmu_pix;

  public:

    const EventImage2D& ev_adc() const { return _ev_adc; }
    const EventImage2D& ev_trk() const { return _ev_trk; }
    const EventImage2D& ev_shr() const { return _ev_shr; }
    const std::vector<Image2D>& adc_img_v() const { return _ev_adc.Image2DArray(); }
    const std::vector<Image2D>& trk_img_v() const { return _ev_trk.Image2DArray(); }
    const std::vector<Image2D>& shr_img_v() const { return _ev_shr.Image2DArray(); }
    const Image2D& adc_img(size_t plane) const { return _ev_adc.Image2DArray().at(plane); }
    const Image2D& trk_img(size_t plane) const { return _ev_trk.Image2DArray().at(plane); }
    const Image2D& shr_img(size_t plane) const { return _ev_shr.Image2DArray().at(plane); }
    
    
    const EventPixel2D& ev_thrumu_pix() const { return _ev_thrumu_pix; }
    const EventPixel2D& ev_stopmu_pix() const { return _ev_stopmu_pix; }
    const std::map<PlaneID_t,std::vector<Pixel2DCluster> >& thrumu_pix_m() const
    { return _ev_thrumu_pix.Pixel2DClusterArray(); }
    const std::map<PlaneID_t,std::vector<Pixel2DCluster> >& stopmu_pix_m() const
    { return _ev_stopmu_pix.Pixel2DClusterArray(); }
    const std::vector<Pixel2DCluster>& thrumu_pix_v(size_t plane) const
    { return _ev_thrumu_pix.Pixel2DClusterArray().at(plane); }
    const std::vector<Pixel2DCluster>& stopmu_pix_v(size_t plane) const
    { return _ev_stopmu_pix.Pixel2DClusterArray().at(plane); }
    
  };

  class LArbysImageExtractProcessFactory : public ProcessFactoryBase {
  public:
    /// ctor
    LArbysImageExtractProcessFactory() { ProcessFactory::get().add_factory("LArbysImageExtract",this); }
    /// dtor
    ~LArbysImageExtractProcessFactory() {}
    /// creation method
    ProcessBase* create(const std::string instance_name) { return new LArbysImageExtract(instance_name); }
  };

}

#endif
/** @} */ // end of doxygen group 
