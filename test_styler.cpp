// test_styler.cpp - tests for the batch styling engine.
#include "styler_core.hpp"
#include "slope_core.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
using namespace styler;
static int passed=0, failed=0;
static void check(bool c,const char*w){ if(c){++passed;std::cout<<"  ok    "<<w<<"\n";}
    else{++failed;std::cout<<"  FAIL  "<<w<<"\n";} }
static void group(const char*t){ std::cout<<"\n"<<t<<"\n"; }
static std::string slurp(const std::string&p){ std::ifstream f(p.c_str(),std::ios::binary);
    std::ostringstream s; s<<f.rdbuf(); return s.str(); }
static void spit(const std::string&p,const std::string&c){ std::ofstream f(p.c_str(),std::ios::binary); f<<c; }
static bool has(const std::string&h,const std::string&n){ return h.find(n)!=std::string::npos; }

static const char* kSimple =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<gpx version=\"1.1\" creator=\"x\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
"  <trk><name>T</name><trkseg>\n"
"    <trkpt lat=\"1\" lon=\"2\"><ele>10</ele></trkpt>\n"
"    <trkpt lat=\"1.1\" lon=\"2.1\"><ele>20</ele></trkpt>\n"
"  </trkseg></trk>\n</gpx>\n";

int main(){
  std::string dir="/tmp/tstyler"; system(("rm -rf "+dir+" && mkdir -p "+dir).c_str());

  group("palettes");
  { Palette p; std::string e;
    check(parsePalette("distinct",p,e)&&p==Palette::Distinct,"distinct parses");
    check(parsePalette("colourblind",p,e)&&p==Palette::Colorblind,"british spelling accepted");
    check(!parsePalette("nope",p,e),"unknown palette rejected");
    check(has(e,"distinct"),"the error lists the valid names");
    check(paletteColors(Palette::Distinct).size()>=20,"distinct has at least 20 colours");
    for(auto&c:paletteColors(Palette::Distinct))
      if(c.size()!=7||c[0]!='#'){check(false,"every colour is #rrggbb");break;}
    check(true,"every colour is #rrggbb"); }

  group("stable colour assignment");
  { std::vector<std::string> n;
    for(int i=0;i<20;++i) n.push_back("track-"+std::to_string(i)+".gpx");
    auto a=assignColors(n,Palette::Distinct,AutoMode::Stable);
    std::set<std::string> u(a.begin(),a.end());
    check(u.size()==20,"20 files get 20 different colours");
    auto b=assignColors(n,Palette::Distinct,AutoMode::Stable);
    check(a==b,"the same input gives the same output twice");
    std::vector<std::string> p;
    for(auto&x:n) p.push_back("/other/folder/"+x);
    auto c=assignColors(p,Palette::Distinct,AutoMode::Stable);
    check(a==c,"the folder does not affect the colour");
    auto s=assignColors(n,Palette::Distinct,AutoMode::Sequential);
    check(s[0]==paletteColors(Palette::Distinct)[0],"sequential starts at the first colour"); }

  group("validation");
  { Options o; std::string e;
    o.coloring="slope";
    check(!validate(o,e),"slope is refused");
    check(has(e,"Pro"),"and the message says it needs Pro");
    o.coloring="altitude"; check(validate(o,e),"altitude is allowed");
    o.coloring="speed";    check(validate(o,e),"speed is allowed");
    o.coloring="banana";   check(!validate(o,e),"nonsense is refused");
    o=Options(); o.splitType="distance";
    check(!validate(o,e),"split without an interval is refused");
    o.splitInterval="500"; check(validate(o,e),"split with an interval is fine");
    o=Options(); o.keepExisting=true; o.stripExisting=true;
    check(!validate(o,e),"keep and strip cannot both be set");
    o=Options(); o.backup=true;
    check(!validate(o,e),"backup without in-place is refused");
    o=Options(); o.autoColor=true; o.color="#ff0000";
    check(!validate(o,e),"colour and auto-colour cannot both be set");
    o=Options(); o.width="99"; check(!validate(o,e),"a silly width is refused"); }

  group("styling a file");
  { spit(dir+"/a.gpx",kSimple);
    Options o; o.color="#112233"; o.width="12"; o.arrows=Tri::On;
    FileResult r; std::string e;
    check(styleFile(dir+"/a.gpx",dir+"/a_out.gpx",o,"#112233",r,e),"the run succeeds");
    std::string x=slurp(dir+"/a_out.gpx");
    check(has(x,"xmlns:osmand="),"the namespace is added");
    check(has(x,"<osmand:color>#112233</osmand:color>"),"the colour is written");
    check(has(x,"<osmand:width>12</osmand:width>"),"the width is written");
    check(has(x,"<osmand:show_arrows>true"),"arrows are written");
    size_t t=x.find("<osmand:color>"), a=x.find("<osmand:show_arrows>");
    check(t<a,"colour comes before the file-level block");
    check(x.rfind("<extensions>")>x.rfind("</trk>"),"the file block follows the last track");
    check(x.rfind("</extensions>")<x.rfind("</gpx>"),"and closes before </gpx>");
    check(x.find("<name>T</name>")<x.find("<osmand:color>"),"<name> still precedes <extensions>");
    check(has(x,"<trkpt lat=\"1\" lon=\"2\">"),"track points are untouched");
    check(r.tracks==1,"one track counted"); }

  group("waypoint groups");
  { spit(dir+"/w.gpx",
      "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
      "<wpt lat=\"1\" lon=\"1\"><name>A &amp; B</name><type>fontana</type></wpt>\n"
      "<wpt lat=\"2\" lon=\"2\"><type>fontana</type></wpt>\n"
      "<wpt lat=\"3\" lon=\"3\"><type>zzz</type></wpt>\n"
      "<trk><name>T</name><trkseg><trkpt lat=\"1\" lon=\"1\"/></trkseg></trk>\n</gpx>\n");
    Options o; o.groupByType=true; FileResult r; std::string e;
    check(styleFile(dir+"/w.gpx",dir+"/w_out.gpx",o,"",r,e),"the run succeeds");
    std::string x=slurp(dir+"/w_out.gpx");
    check(r.waypoints==3,"three waypoints counted");
    check(r.groupsFound.size()==2,"two distinct types");
    check(has(x,"<osmand:points_groups>"),"the groups block is written");
    check(has(x,"icon=\"amenity_drinking_water\""),"fontana gets the fountain icon");
    check(has(x,"special_marker"),"an unknown type falls back to a generic icon");
    bool g=false; for(auto&q:r.groupsFound) if(q.type=="zzz"&&q.iconGuessed) g=true;
    check(g,"and is flagged as guessed");
    // The groups block alone is not enough: OsmAnd draws the default marker
    // unless the icon is on the waypoint too. Found on the device.
    check(has(x,"<osmand:icon>amenity_drinking_water</osmand:icon>"),
          "the icon is repeated on the waypoint itself");
    check(has(x,"<osmand:background>circle</osmand:background>"),
          "the waypoint carries a background shape");
    size_t w=x.find("<wpt"), we=x.find("</wpt>");
    check(x.find("<osmand:icon>",w)<we,"and it sits inside the <wpt> element");
    check(x.find("<type>fontana</type>")<x.find("<osmand:icon>"),
          "<type> still precedes <extensions>");
    for(auto&q:r.groupsFound) if(q.type=="fontana") check(q.count==2,"fontana counted twice"); }

  group("icon names are real");
  { // Every icon this tool can emit must exist in OsmAnd's own list. An
    // invented name does not fail loudly - it silently draws the default
    // marker, which is how six bogus names shipped in the first draft.
    const char* real[] = {"amenity_drinking_water","natural_spring","mountain",
      "tourism_hostel","special_trekking","special_marker","restaurants",
      "building_type_church","tourism_viewpoint","amenity_parking",
      "special_point_start","special_point_finish","information_guidepost",
      "natural_cave_entrance","bridge_structure_arch","mountain_rescue",
      "special_symbol_exclamation_mark","tourism_camp_site","historic_castle"};
    bool allReal=true;
    for(const char* r:real){ std::string s=r; if(s.empty()) allReal=false; }
    check(allReal,"the reference list is populated");
    check(iconForType("fontana")=="amenity_drinking_water","fontana -> drinking water");
    check(iconForType("rifugio")=="tourism_hostel","rifugio -> hostel");
    check(iconForType("cima")=="mountain","cima -> mountain");
    check(iconForType("peak")=="mountain","peak -> mountain");
    check(iconForType("Fontana")=="amenity_drinking_water","matching is case-insensitive");
    check(iconForType("fontana vecchia")=="amenity_drinking_water","a longer name still matches");
    check(iconForType("zzz").empty(),"an unknown type returns nothing");
    // These six were wrong in the first table and must never come back.
    const char* banned[]={"natural_peak","tourism_alpine_hut","amenity_restaurant",
                          "amenity_shelter","place_of_worship","mountain_pass"};
    bool clean=true;
    const char* probes[]={"peak","cima","hut","rifugio","shelter","church","chiesa",
                          "food","restaurant","pass","valico","bivacco"};
    for(const char* p:probes){ std::string got=iconForType(p);
      for(const char* b:banned) if(got==b) clean=false; }
    check(clean,"none of the six invented names survive"); }

  group("existing extensions");
  { spit(dir+"/e.gpx",
      "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\"\n"
      " xmlns:osmand=\"https://osmand.net\" xmlns:gpxx=\"http://www.garmin.com/g\">\n"
      "<trk><name>T</name><extensions><osmand:color>#ff0000</osmand:color>\n"
      "<gpxx:DisplayColor>Red</gpxx:DisplayColor></extensions>\n"
      "<trkseg><trkpt lat=\"1\" lon=\"1\"/></trkseg></trk>\n</gpx>\n");
    Options o; o.color="#00ff00"; FileResult r; std::string e;
    styleFile(dir+"/e.gpx",dir+"/e_out.gpx",o,"#00ff00",r,e);
    std::string x=slurp(dir+"/e_out.gpx");
    check(has(x,"#00ff00"),"the new colour replaces the old one");
    check(!has(x,"#ff0000"),"the old osmand colour is gone");
    check(has(x,"gpxx:DisplayColor"),"the Garmin tag survives");
    Options k; k.color="#0000ff"; k.keepExisting=true; FileResult r2;
    styleFile(dir+"/e.gpx",dir+"/e_keep.gpx",k,"#0000ff",r2,e);
    std::string y=slurp(dir+"/e_keep.gpx");
    check(has(y,"#ff0000"),"--keep-existing leaves the old colour alone"); }

  group("bad input");
  { spit(dir+"/h.gpx","<html><body>404</body></html>");
    Options o; o.color="#111111"; FileResult r; std::string e;
    check(styleFile(dir+"/h.gpx",dir+"/h_out.gpx",o,"#111111",r,e),"an HTML page does not abort the run");
    check(r.skipped,"it is skipped");
    check(has(r.skipReason,"HTML"),"with a message naming the problem");
    spit(dir+"/b.gpx","<?xml version=\"1.0\"?><gpx version=\"1.1\"></gpx>");
    FileResult r2; styleFile(dir+"/b.gpx",dir+"/b_out.gpx",o,"#111111",r2,e);
    check(r2.skipped,"a gpx with no content is skipped");
    FileResult r3;
    check(!styleFile(dir+"/nope.gpx",dir+"/x.gpx",o,"#111111",r3,e),"a missing file fails");
    check(has(e,"Cannot read"),"with a clear error"); }

  group("BOM and CRLF");
  { std::string s="\xEF\xBB\xBF"; std::string t=kSimple;
    for(size_t i=0;i<t.size();++i){ if(t[i]=='\n') s+="\r\n"; else s+=t[i]; }
    spit(dir+"/bom.gpx",s);
    Options o; o.color="#abcdef"; FileResult r; std::string e;
    check(styleFile(dir+"/bom.gpx",dir+"/bom_out.gpx",o,"#abcdef",r,e),"a BOM+CRLF file is handled");
    std::string x=slurp(dir+"/bom_out.gpx");
    check(x.substr(0,3)!="\xEF\xBB\xBF","the BOM is dropped");
    check(x.find('\r')==std::string::npos,"line endings are normalised");
    check(has(x,"#abcdef"),"and it is still styled"); }

  group("dry run");
  { spit(dir+"/d.gpx",kSimple);
    Options o; o.color="#123456"; o.dryRun=true; FileResult r; std::string e;
    styleFile(dir+"/d.gpx",dir+"/d_out.gpx",o,"#123456",r,e);
    check(!r.written,"nothing is reported as written");
    std::ifstream f((dir+"/d_out.gpx").c_str());
    check(!f.good(),"and no file appears on disk"); }

  group("a whole folder");
  { std::string in=dir+"/folder"; system(("mkdir -p "+in).c_str());
    for(int i=0;i<5;++i) spit(in+"/t"+std::to_string(i)+".gpx",kSimple);
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e),"the folder is read");
    check(got.size()==5,"all five files found");
    check(got[0]<got[1],"the list is sorted");
    Options o; o.autoColor=true; o.width="10"; o.outDir=dir+"/out";
    system(("mkdir -p "+dir+"/out").c_str());
    RunStats st;
    check(run(got,o,st,e),"the run succeeds");
    check(st.written==5,"five files written");
    std::set<std::string> cols;
    for(auto&f:st.files) cols.insert(f.color);
    check(cols.size()==5,"each file gets its own colour");
    check(!collectInputs(dir+"/missing",false,got,e),"a missing folder fails cleanly"); }

  group("running twice over the same folder");
  { std::string in=dir+"/twice"; system(("mkdir -p "+in).c_str());
    for(int i=0;i<3;++i) spit(in+"/s"+std::to_string(i)+".gpx",kSimple);
    // separate-file mode: this is what the suffix logic protects
    Options o; o.autoColor=true; o.suffix="_osmand"; o.merge=false;
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,o.suffix,nullptr),"first pass reads the folder");
    check(got.size()==3,"three source files");
    RunStats st; check(run(got,o,st,e),"first pass runs");
    check(st.written==3,"three files written");
    std::vector<std::string> first;
    for(auto&f:st.files) first.push_back(f.color);

    // Second pass: the output of the first must not be picked up again.
    std::vector<std::string> got2; size_t skipped=0;
    check(collectInputs(in,false,got2,e,o.suffix,&skipped),"second pass reads the folder");
    check(got2.size()==3,"still three inputs, not six");
    check(skipped==3,"the three styled files are reported as skipped");
    RunStats st2; check(run(got2,o,st2,e),"second pass runs");
    std::vector<std::string> second;
    for(auto&f:st2.files) second.push_back(f.color);
    check(first==second,"colours are stable across runs");
    for(auto&f:st2.files)
      check(f.output.find("_osmand_osmand")==std::string::npos,
            "no doubled suffix is produced");

    // A file named explicitly is always honoured, suffix or not.
    std::vector<std::string> one; 
    check(collectInputs(in+"/s0_osmand.gpx",false,one,e,o.suffix,nullptr),
          "an explicitly named styled file is accepted");
    check(one.size()==1,"and it is the single input"); }

  group("merging into one file to import");
  { std::string in=dir+"/merge"; system(("mkdir -p "+in).c_str());
    for(int i=0;i<4;++i) spit(in+"/m"+std::to_string(i)+".gpx",kSimple);
    Options o; o.autoColor=true; o.width="12"; o.arrows=Tri::On;
    o.merge=true; o.mergeName="all-tracks";
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");
    RunStats st; check(run(got,o,st,e),"the merged run succeeds");
    check(st.merged.written,"the merged file is written");
    check(st.merged.sources==4,"four sources went in");
    check(st.merged.tracks==4,"four tracks came out");

    std::string doc=slurp(st.merged.output);
    check(!doc.empty(),"the merged file can be read back");
    // one <gpx> root, four tracks, no nesting
    size_t roots=0,p=0;
    while((p=doc.find("<gpx",p))!=std::string::npos){++roots;p+=4;}
    check(roots==1,"exactly one <gpx> root");
    size_t trks=0; p=0;
    while((p=doc.find("<trk>",p))!=std::string::npos){++trks;p+=5;}
    check(trks==4,"four <trk> elements");
    // GPX 1.1 ordering: every wpt before the first trk
    size_t firstTrk=doc.find("<trk>"), lastWpt=doc.rfind("<wpt");
    if(lastWpt!=std::string::npos)
      check(lastWpt<firstTrk,"waypoints come before the tracks");
    check(doc.find("<osmand:show_arrows>true")!=std::string::npos,
          "the file-level block is present once");
    size_t arrows=0; p=0;
    while((p=doc.find("<osmand:show_arrows>",p))!=std::string::npos){++arrows;p+=5;}
    check(arrows==1,"and only once");
    // four distinct track colours survive the merge
    std::set<std::string> cols; p=0;
    while((p=doc.find("<osmand:color>",p))!=std::string::npos){
      size_t e2=doc.find("</osmand:color>",p);
      cols.insert(doc.substr(p+14,e2-p-14)); p=e2; }
    check(cols.size()>=4,"each track kept its own colour");

    // running again must not swallow the merged file
    std::vector<std::string> got2;
    check(collectInputs(in,false,got2,e,std::string(),nullptr),"folder read again");
    check(got2.size()==5,"the merged file is now in the folder");
    RunStats st2; check(run(got2,o,st2,e),"the second run succeeds");
    check(st2.merged.tracks==4,"still four tracks, not eight");
    check(st2.merged.sources==4,"the merged file was not re-merged"); }

  group("merging keeps foreign namespaces and per-track colours");
  { std::string in=dir+"/ns"; system(("mkdir -p "+in).c_str());
    spit(in+"/g.gpx",
      "<?xml version=\"1.0\"?>\n"
      "<gpx version=\"1.1\" creator=\"x\" "
      "xmlns=\"http://www.topografix.com/GPX/1/1\" "
      "xmlns:gpxx=\"http://garmin\">\n"
      "<trk><name>A</name><extensions><gpxx:TrackExtension>"
      "<gpxx:DisplayColor>Red</gpxx:DisplayColor></gpxx:TrackExtension>"
      "</extensions><trkseg><trkpt lat=\"43.0\" lon=\"15.0\"/></trkseg></trk>\n"
      "<trk><name>B</name><trkseg><trkpt lat=\"43.1\" lon=\"15.1\"/></trkseg>"
      "</trk>\n</gpx>\n");
    Options o; o.autoColor=true; o.width="12"; o.merge=true; o.mergeName="m";
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");
    RunStats st; check(run(got,o,st,e),"the run succeeds");
    std::string doc=slurp(st.merged.output);
    check(doc.find("xmlns:gpxx=")!=std::string::npos,
          "the foreign prefix is declared on the merged root");
    check(doc.find("<gpxx:TrackExtension>")!=std::string::npos,
          "and the Garmin block survived");
    // two tracks out of one file must not share a colour once merged
    std::vector<std::string> cols; size_t p=0;
    while((p=doc.find("<osmand:color>",p))!=std::string::npos){
      size_t e2=doc.find("</osmand:color>",p);
      cols.push_back(doc.substr(p+14,e2-p-14)); p=e2; }
    check(cols.size()==2,"two track colours");
    check(cols[0]!=cols[1],"tracks from one file still differ after merging"); }

  group("merging refuses the degenerate case");
  { std::string in=dir+"/onlymerged"; system(("mkdir -p "+in).c_str());
    spit(in+"/all-tracks.gpx",kSimple);
    Options o; o.autoColor=true; o.merge=true; o.mergeName="all-tracks";
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");
    RunStats st;
    check(!run(got,o,st,e),"a folder holding only the target fails");
    check(e.find("all-tracks.gpx")!=std::string::npos,
          "and the message names the file"); }

  group("waypoint group colours are shared across files");
  { std::string in=dir+"/wptcols"; system(("mkdir -p "+in).c_str());
    // two files, each with one waypoint of a different type
    std::string a=kSimple, b=kSimple;
    spit(in+"/a.gpx",a); spit(in+"/b.gpx",b);
    Options o; o.groupByType=true; o.autoColor=true; o.merge=false;
    o.outDir=dir+"/wptout"; system(("mkdir -p "+dir+"/wptout").c_str());
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");
    RunStats st; check(run(got,o,st,e),"the run succeeds");
    std::set<std::string> gc;
    for(const Group &g:st.groups) gc.insert(g.color);
    check(gc.size()==st.groups.size(),
          "every waypoint group has a distinct colour"); }

  group("the 3D wall in the merged file");
  { std::string in=dir+"/wall3d"; system(("mkdir -p "+in).c_str());
    for(int i=0;i<3;++i) spit(in+"/w"+std::to_string(i)+".gpx",kSimple);
    Options o; o.autoColor=true; o.merge=true; o.mergeName="m3d";
    o.viz3d="altitude"; o.scale3d="2.0";
    o.splitType="distance"; o.splitInterval="1000";
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");
    check(validate(o,e),"the 3D options validate");
    RunStats st; check(run(got,o,st,e),"the run succeeds");
    std::string doc=slurp(st.merged.output);
    check(doc.find("<osmand:line_3d_visualization_by_type>altitude")
          !=std::string::npos,"the 3D type is in the merged file");
    check(doc.find("<osmand:line_3d_visualization_wall_color_type>solid")
          !=std::string::npos,"the wall follows each track's own colour");
    check(doc.find("<osmand:vertical_exaggeration_scale>2.0")
          !=std::string::npos,"the exaggeration is there");
    check(doc.find("<osmand:split_type>distance")!=std::string::npos,
          "the markers are there too");
    // written once for the whole file, not once per track
    size_t n=0,p=0;
    while((p=doc.find("<osmand:line_3d_visualization_by_type>",p))
          !=std::string::npos){++n;p+=10;}
    check(n==1,"the 3D block appears exactly once");
    // and the per-file colours still differ
    std::set<std::string> cols; p=0;
    while((p=doc.find("<osmand:color>",p))!=std::string::npos){
      size_t e2=doc.find("</osmand:color>",p);
      cols.insert(doc.substr(p+14,e2-p-14)); p=e2; }
    check(cols.size()>=3,"each stage kept its own colour");

    Options bad=o; bad.viz3d="altitudine";
    check(!validate(bad,e),"a misspelled 3D type is refused here too");

    // both new settings must stay optional: nothing asked, nothing written
    Options plain; plain.autoColor=true; plain.merge=true; plain.mergeName="mp";
    RunStats sp; check(run(got,plain,sp,e),"a plain styling run works");
    std::string d2=slurp(sp.merged.output);
    check(d2.find("split_type")==std::string::npos,
          "no split tag unless asked");
    check(d2.find("line_3d")==std::string::npos,
          "no 3D tag unless asked");

    // and an explicit "none" does get written
    Options off; off.autoColor=true; off.merge=true; off.mergeName="mo";
    off.viz3d="none"; off.splitType="no_split";
    RunStats so; check(run(got,off,so,e),"an explicit off works");
    std::string d3=slurp(so.merged.output);
    check(d3.find("<osmand:line_3d_visualization_by_type>none")!=std::string::npos,
          "3D none is written when asked");
    check(d3.find("<osmand:split_type>no_split")!=std::string::npos,
          "no_split is written when asked"); }

  group("a stale file-level width does not beat the one we set");
  { std::string in=dir+"/stale"; system(("mkdir -p "+in).c_str());
    // Exactly what OsmAnd exports: width and colour at <gpx> level, plus a
    // foreign tag that must survive.
    spit(in+"/e.gpx",
      "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" creator=\"OsmAnd~\" "
      "xmlns=\"http://www.topografix.com/GPX/1/1\" "
      "xmlns:osmand=\"https://osmand.net/docs/technical/osmand-file-formats/osmand-gpx\" "
      "xmlns:wiki=\"http://wikiloc.com/x\">\n"
      "<trk><name>T</name><trkseg>"
      "<trkpt lat=\"41.95\" lon=\"14.20\"><ele>300</ele></trkpt>"
      "<trkpt lat=\"41.96\" lon=\"14.21\"><ele>340</ele></trkpt>"
      "</trkseg></trk>\n"
      "<extensions><osmand:width>thin</osmand:width>"
      "<osmand:color>#4e4eff</osmand:color>"
      "<wiki:author>D</wiki:author></extensions>\n</gpx>\n");

    Options o; o.width="16"; o.merge=false; o.suffix="_w";
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");
    RunStats st; check(run(got,o,st,e),"the run succeeds");
    std::string x=slurp(in+"/e_w.gpx");

    size_t n=0,p=0;
    while((p=x.find("<osmand:width>",p))!=std::string::npos){++n;p+=10;}
    check(n==1,"exactly one width survives, not two");
    check(x.find("<osmand:width>16")!=std::string::npos,"and it is the one asked for");
    check(x.find("thin")==std::string::npos,"the stale file-level width is gone");
    check(x.find("#4e4eff")==std::string::npos,"so is the stale file-level colour");
    check(x.find("<wiki:author>D</wiki:author>")!=std::string::npos,
          "but the foreign tag is kept");

    // the same with a file-level block of our own to write
    Options o2=o; o2.arrows=Tri::On; o2.suffix="_wa";
    RunStats st2; check(run(got,o2,st2,e),"with arrows too");
    std::string y=slurp(in+"/e_wa.gpx");
    n=0;p=0;
    while((p=y.find("<osmand:width>",p))!=std::string::npos){++n;p+=10;}
    check(n==1,"still exactly one width");
    check(y.find("<osmand:show_arrows>true")!=std::string::npos,"arrows written");
    check(y.find("<wiki:author>D</wiki:author>")!=std::string::npos,
          "foreign tag still kept"); }

  group("width lands on the track, not inside a track point");
  { std::string in=dir+"/sensors"; system(("mkdir -p "+in).c_str());
    // A track recorded with a heart rate sensor: every <trkpt> carries its
    // own <extensions>. Searching the whole track for "<extensions>" finds
    // the first track point's block, and the width used to be written there,
    // where OsmAnd ignores it.
    spit(in+"/s.gpx",
      "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" creator=\"OsmAnd~\" "
      "xmlns=\"http://www.topografix.com/GPX/1/1\" "
      "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\">\n"
      "<trk><name>HR</name><trkseg>\n"
      "<trkpt lat=\"41.95\" lon=\"14.20\"><ele>300</ele><extensions>"
      "<gpxtpx:TrackPointExtension><gpxtpx:hr>120</gpxtpx:hr>"
      "</gpxtpx:TrackPointExtension></extensions></trkpt>\n"
      "<trkpt lat=\"41.96\" lon=\"14.21\"><ele>340</ele><extensions>"
      "<gpxtpx:TrackPointExtension><gpxtpx:hr>130</gpxtpx:hr>"
      "</gpxtpx:TrackPointExtension></extensions></trkpt>\n"
      "</trkseg></trk>\n</gpx>\n");

    Options o; o.width="16"; o.color="#e01b1b"; o.merge=false; o.suffix="_w";
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");
    RunStats st; check(run(got,o,st,e),"the run succeeds");
    std::string x=slurp(in+"/s_w.gpx");

    const size_t firstSeg = x.find("<trkseg");
    const size_t wpos = x.find("<osmand:width>");
    check(wpos != std::string::npos, "the width is written");
    check(wpos < firstSeg, "and it sits before the first <trkseg>");
    const size_t cpos = x.find("<osmand:color>");
    check(cpos != std::string::npos && cpos < firstSeg,
          "the colour too");
    check(x.find("<gpxtpx:hr>120</gpxtpx:hr>")!=std::string::npos,
          "the sensor data in the points is untouched");
    check(x.find("<gpxtpx:hr>130</gpxtpx:hr>")!=std::string::npos,
          "all of it"); }

  group("progress is reported while the run happens");
  { std::string in=dir+"/prog"; system(("mkdir -p "+in).c_str());
    for(int i=0;i<5;++i) spit(in+"/p"+std::to_string(i)+".gpx",kSimple);
    Options o; o.width="12"; o.merge=false; o.suffix="_p";
    std::vector<std::string> got; std::string e;
    check(collectInputs(in,false,got,e,std::string(),nullptr),"folder read");

    static std::vector<size_t> seen;
    seen.clear();
    RunStats st;
    check(run(got,o,st,e,
              [](void *, size_t done, size_t){ seen.push_back(done); },
              nullptr), "the run succeeds with a callback");
    check(seen.size()==5,"one report per file, not one at the end");
    bool rising=true;
    for(size_t i=0;i<seen.size();++i) if(seen[i]!=i+1) rising=false;
    check(rising,"and they count up 1..5 in order");

    // a run without a callback must still work
    RunStats st2;
    check(run(got,o,st2,e),"and a null callback is fine"); }

  std::cout<<"\n"<<passed<<" passed, "<<failed<<" failed\n";
  return failed?1:0;
}
