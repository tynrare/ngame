// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | a6083f
/* Flow ID: fonts-comparison (canonical owner).
 * 1) scene init/start → register two fonts and spawn examples → persistent labels.
 * 2) draw → submit immediate text and update created text → matching style/geometry.
 * 3) first draw → submit flash once → only created survivor remains next frame.
 * 4) scene stop/dispose → despawn and release recipes → neither lifetime crosses unload.
 * Invariant: paired poses differ only by horizontal translation for comparison.
 */
function Label() {}
function Scene() {}
/** @return void; register camera scopes, font materials and entity recipes. */
Scene.prototype.init=function() {
  global.describe("font","sans",{src:"fonts/LiberationSans-Regular.ttf"});
  global.describe("font","rubik",{src:"fonts/Rubik-ExtraBold.ttf"});
  global.describe("scope","world",{render: "simple",bg:{r:12,g:14,b:22},
    camera:{mode:"fixed",position:{x:0,y:0,z:6},target:{x:0,y:0,z:0},fovy:45}});
  global.describe("scope","screen",{camera:{mode:"ortho"}});
  global.describe("scene","view",{scopes:["world","screen"]});
  global.describe("label","sans_e",{font:"sans",func:Label,sync:"shared"});
  global.describe("label","rubik_e",{font:"rubik",func:Label,sync:"shared"});
  global.describe("mesh","backdrop_m",{shape:"cube",width:1,height:1,depth:1});
  global.describe("shader","backdrop_s",{vertex:"shaders/mesh.vs",fragment:"shaders/flat.fs",tint:{r:70,g:90,b:120}});
  global.describe("model","backdrop",{mesh:"backdrop_m",shader:"backdrop_s"});
  global.describe("entity","backdrop_e",{model:"backdrop",func:Label,sync:"shared"});
};
/** @param session Object session. @return void; ordinary entities provide persistent comparisons. */
Scene.prototype.start=function(session) {
  this.frames=0; this.pairs=[];
  this.backdrop=global.spawn("backdrop_e",{scope:"world",position:{x:0,y:0,z:-1},scale:0.25});
  for(var i=0;i<4;i++) {
    var screen=i<2, font=i%2?"rubik":"sans";
    var style={scope:screen?"screen":"world",size:screen?23:0.24,outline:0.08,
      tint:{r:150,g:235,b:190},rotation:{x:0,y:0,z:0.03},scale:1,
      position:screen?{x:30,y:125+i*50,z:0}:{x:-2.3,y:-0.65-(i-2)*0.55,z:0.5}};
    var created={scope:style.scope,size:style.size,outline:style.outline,tint:style.tint,
      rotation:style.rotation,scale:style.scale,
      position:{x:screen?425:0.3,y:style.position.y,z:style.position.z},text:""};
    this.pairs.push({font:font,style:style,handle:global.spawn(font+"_e",created)});
  }
  this.survivor=global.spawn("sans_e",{scope:"screen",position:{x:425,y:405,z:0},size:17,text:"Survives until despawn"});
};
/** @return void; transient submissions last one frame, persistent text updates in place. */
Scene.prototype.draw=function() {
  this.frames++;
  global.draw_label("sans","Immediate",{scope:"screen",position:{x:30,y:65,z:0},size:26});
  global.draw_label("sans","Created entity",{scope:"screen",position:{x:425,y:65,z:0},size:26});
  global.draw_label("sans","Screen coordinates",{scope:"screen",position:{x:30,y:95,z:0},size:16});
  global.draw_label("sans","World billboards",{scope:"screen",position:{x:30,y:255,z:0},size:16});
  for(var i=0;i<this.pairs.length;i++) {
    var p=this.pairs[i], text=p.font+" frame "+this.frames;
    global.draw_label(p.font,text,p.style); global.set_text(p.handle,text);
  }
  global.draw_label("sans","One-frame flash below (then empty)",{scope:"screen",position:{x:30,y:375,z:0},size:16});
  if(this.frames===1) global.draw_label("sans","Only this frame",{scope:"screen",position:{x:30,y:405,z:0},size:17});
};
/** @return void; explicitly end persistent lifetimes. */
Scene.prototype.stop=function() {
  for(var i=0;i<this.pairs.length;i++) global.despawn(this.pairs[i].handle);
  global.despawn(this.survivor); global.despawn(this.backdrop);
};
/** @return void; release comparison recipes and material handles. */
Scene.prototype.dispose=function() {
  global.dispose("label","sans_e"); global.dispose("label","rubik_e");
  global.dispose("font","sans"); global.dispose("font","rubik");
  global.dispose("entity","backdrop_e"); global.dispose("model","backdrop");
  global.dispose("shader","backdrop_s"); global.dispose("mesh","backdrop_m");
  global.dispose("scope","world"); global.dispose("scope","screen");
};
global.module(Scene);
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | a6083f
