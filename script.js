// ================= KONFIG (SATU CHANNEL) =================
const CH = {
  id: "2604635",
  readKey: "MASUKKAN_READ_API_KEY_DI_SINI" // ambil dari ThingSpeak > channel > API Keys
};

// ================= CHART FACTORY =================
function makeChart(ctx, label, color){
  return new Chart(ctx, {
    type:'line',
    data:{ labels:[], datasets:[{ label, data:[], borderColor:color, backgroundColor:color+'22', fill:true, tension:0.4, pointRadius:2 }] },
    options:{
      responsive:true,
      animation:{ duration:400 },
      plugins:{ legend:{ display:false } },
      scales:{
        x:{ ticks:{ color:'#6b6558', maxTicksLimit:6 }, grid:{ display:false } },
        y:{ ticks:{ color:'#6b6558' }, grid:{ color:'#eee6d3' } }
      }
    }
  });
}

const tempChart = makeChart(document.getElementById("tempChart"), 'Suhu (°C)', '#ef4444');
const humChart  = makeChart(document.getElementById("humChart"),  'Kelembapan (%)', '#2f6fd8');
const soilChart = makeChart(document.getElementById("soilChart"), 'Tanah (%)', '#b8843c');

// ================= STATUS UI =================
function setStatus(online){
  const led = document.getElementById("statusLed");
  const text = document.getElementById("statusText");
  led.className = "led " + (online ? "online" : "offline");
  text.textContent = online ? "Terhubung ke ThingSpeak" : "Gagal memuat data";
}

function setBadge(el, isOn){
  el.textContent = isOn ? "NYALA" : "MATI";
  el.className = "badge" + (isOn ? " on" : "");
}

// ================= LOAD DATA =================
async function loadData(){
  try{
    const r = await fetch(`https://api.thingspeak.com/channels/${CH.id}/feeds.json?api_key=${CH.readKey}&results=20`);
    const d = await r.json();

    tempChart.data.labels = []; tempChart.data.datasets[0].data = [];
    humChart.data.labels  = []; humChart.data.datasets[0].data  = [];
    soilChart.data.labels = []; soilChart.data.datasets[0].data = [];

    let lastT=null, lastH=null, lastS=null, lastR1=null, lastR3=null;

    d.feeds.forEach(f=>{
      const time = new Date(f.created_at).toLocaleTimeString('id-ID',{hour:'2-digit',minute:'2-digit'});

      const temp = parseFloat(f.field1);
      if(!isNaN(temp)){ tempChart.data.labels.push(time); tempChart.data.datasets[0].data.push(temp); lastT = temp; }

      const hum = parseFloat(f.field2);
      if(!isNaN(hum)){ humChart.data.labels.push(time); humChart.data.datasets[0].data.push(hum); lastH = hum; }

      const soil = parseFloat(f.field3);
      if(!isNaN(soil)){ soilChart.data.labels.push(time); soilChart.data.datasets[0].data.push(soil); lastS = soil; }

      const r1 = parseFloat(f.field4);
      if(!isNaN(r1)) lastR1 = r1;

      const r3 = parseFloat(f.field5);
      if(!isNaN(r3)) lastR3 = r3;
    });

    tempChart.update(); humChart.update(); soilChart.update();

    if(lastT!==null) document.getElementById("lcdSuhu").innerHTML = lastT.toFixed(2)+"<small>°C</small>";
    if(lastH!==null) document.getElementById("lcdHum").innerHTML  = lastH.toFixed(2)+"<small>%</small>";
    if(lastS!==null) document.getElementById("lcdSoil").innerHTML = lastS.toFixed(0)+"<small>%</small>";

    if(lastR1!==null) setBadge(document.getElementById("badge1"), lastR1 === 1);
    if(lastR3!==null) setBadge(document.getElementById("badge3"), lastR3 === 1);

    setStatus(true);
  }catch(err){
    console.log("ERROR:", err);
    setStatus(false);
  }
}

// ================= AUTO UPDATE =================
loadData();
setInterval(loadData, 15000);
