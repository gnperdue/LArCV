//
// cint script to generate libraries
// Declaire namespace & classes you defined
// #pragma statement: order matters! Google it ;)
//

#ifdef __CINT__
#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ class larcv::LArbysImage+;
#pragma link C++ class larcv::LArbysImageMaker+;
#pragma link C++ class larcv::LArbysImageMC+;

#pragma link C++ class larcv::PreProcessor+;
#pragma link C++ class larcv::PreProcessorAna+;
#pragma link C++ class larcv::TrackShowerAna+;
#pragma link C++ class larcv::LArOCVSerial+;
#pragma link C++ class larcv::LArbysRecoHolder+;
#pragma link C++ class larcv::LArbysImageMaker+;
#pragma link C++ class larcv::LArbysImageExtract+;
#pragma link C++ class larcv::LEE1e1p+;
#pragma link C++ class larcv::VertexInROI+;

#pragma link C++ namespace handshake+;
#pragma link C++ class handshake::HandShaker+;

//thx kazu
#pragma link C++ class std::vector<std::vector<std::vector<std::pair<size_t,size_t> > > >+;
#pragma link C++ class std::vector<std::vector<larcv::ImageMeta> >+;

//ADD_NEW_CLASS ... do not change this line
#endif






































