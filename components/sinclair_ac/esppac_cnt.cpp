// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esppac_cnt.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#if defined(USE_ESP8266)
#include <ESP8266WebServer.h>
#elif defined(USE_ESP32)
#include <WebServer.h>
#endif

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sinclair_ac {
namespace CNT {

static const char *const TAG = "sinclair_ac.serial";

static const char DEBUG_UI_HTML[] = R"HTML(
<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Sinclair AC Debug</title>
    <style>
        :root{--bg:#0e1118;--panel:#171d2a;--text:#e8edf5;--muted:#95a3b8;--ok:#2fc97b;--bad:#f05d5e}
        body{font:14px/1.4 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;background:linear-gradient(160deg,#0b0f17,#121a28);color:var(--text);margin:0;padding:14px}
        h1{font-size:16px;margin:0 0 12px}
        .grid{display:grid;gap:10px;grid-template-columns:1fr}
        .card{background:var(--panel);border:1px solid #2b3449;border-radius:10px;padding:10px}
        .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
        input,select,button{background:#111827;color:var(--text);border:1px solid #36445f;border-radius:6px;padding:6px 8px}
        button{cursor:pointer}
        .muted{color:var(--muted)}
        .ok{color:var(--ok)}
        .bad{color:var(--bad)}
        table{width:100%;border-collapse:collapse}
        td,th{border-bottom:1px solid #2a3347;padding:4px 2px;text-align:left;font-size:12px;vertical-align:top}
        .hex{word-break:break-all}
        .cfg-label{padding:4px 6px;border-radius:6px;border:1px solid #36445f}
        .hex-byte{display:inline-block;padding:0 2px;border-radius:3px;border:1px solid transparent}
    </style>
</head>
<body>
    <h1>Sinclair AC Protocol Debug</h1>
    <div class="grid">
        <div class="card">
            <div class="row"><strong>Status</strong> <span id="ready" class="muted">...</span></div>
            <div class="row muted" id="meta"></div>
            <div class="row muted" id="climate"></div>
        </div>
        <div class="card">
            <div class="row"><strong>Structured Control</strong></div>
            <div class="row">
                <select id="power"><option value="">Power (keep)</option><option value="on">On (cool)</option><option value="off">Off</option></select>
                <select id="mode"><option value="">Mode (keep)</option><option>cool</option><option>dry</option><option>fan_only</option></select>
                <input id="temp" type="number" step="1" min="16" max="30" placeholder="Temp">
                <select id="fan"><option value="">Fan (keep)</option><option>auto</option><option>low</option><option>med</option><option>high</option><option>turbo</option></select>
                <button onclick="sendControl()">Queue Send</button>
            </div>
            <div class="row">
                <select id="vswing"><option value="">V Swing (keep)</option><option value="off">off</option><option value="full">full</option><option value="up">up</option><option value="mid">mid</option><option value="down">down</option></select>
                <select id="hswing"><option value="">H Swing (keep)</option><option value="off">off</option><option value="full">full</option><option value="left">left</option><option value="mid">mid</option><option value="right">right</option></select>
                <select id="display"><option value="">Display (keep)</option><option value="off">off</option><option value="auto">auto</option><option value="set">set</option><option value="act">act</option><option value="out">out</option></select>
                <select id="unit"><option value="">Unit (keep)</option><option value="c">C</option><option value="f">F</option></select>
            </div>
            <div class="row">
                <label><input id="plasma" type="checkbox">plasma</label>
                <label><input id="sleep" type="checkbox">sleep</label>
                <label><input id="xfan" type="checkbox">xfan</label>
                <label><input id="save" type="checkbox">save</label>
            </div>
            <div id="control_result" class="muted"></div>
        </div>
        <div class="card">
            <div class="row"><strong>Raw Packet Send</strong></div>
            <div class="row"><input id="hex" style="min-width:320px;flex:1" placeholder="7E 7E ..."><button onclick="sendRaw()">Send Raw</button></div>
            <div id="raw_result" class="muted"></div>
        </div>
        <div class="card">
            <div class="row"><strong>Protocol Config</strong> <button onclick="toggleProtocolConfig()" style="margin-left:auto">Toggle</button></div>
            <div id="protocol_config" style="display:none">
                <div class="row">
                    <label class="cfg-label" data-field="pwr_byte">PWR_BYTE <input id="pwr_byte" type="number" value="4" style="width:60px"></label>
                    <label class="cfg-label" data-field="mode_byte">MODE_BYTE <input id="mode_byte" type="number" value="6" style="width:60px"></label>
                    <label class="cfg-label" data-field="temp_set_lo_byte">TEMP_SET_LO_BYTE <input id="temp_set_lo_byte" type="number" value="10" style="width:60px"></label>
                    <label class="cfg-label" data-field="temp_set_hi_byte">TEMP_SET_HI_BYTE <input id="temp_set_hi_byte" type="number" value="11" style="width:60px"></label>
                    <label class="cfg-label" data-field="temp_act_byte">TEMP_ACT_BYTE <input id="temp_act_byte" type="number" value="20" style="width:60px"></label>
                    <label class="cfg-label" data-field="fan_spd1_byte">FAN_SPD1_BYTE <input id="fan_spd1_byte" type="number" value="7" style="width:60px"></label>
                    <label class="cfg-label" data-field="fan_spd2_byte">FAN_SPD2_BYTE <input id="fan_spd2_byte" type="number" value="4" style="width:60px"></label>
                    <label class="cfg-label" data-field="hswing_byte">HSWING_BYTE <input id="hswing_byte" type="number" value="8" style="width:60px"></label>
                    <label class="cfg-label" data-field="vswing_byte">VSWING_BYTE <input id="vswing_byte" type="number" value="9" style="width:60px"></label>
                    <label class="cfg-label" data-field="temp_set_byte">TEMP_SET_BYTE <input id="temp_set_byte" type="number" value="10" style="width:60px"></label>
                    <label class="cfg-label" data-field="disp_on_byte">DISP_ON_BYTE <input id="disp_on_byte" type="number" value="6" style="width:60px"></label>
                    <label class="cfg-label" data-field="disp_mode_byte">DISP_MODE_BYTE <input id="disp_mode_byte" type="number" value="9" style="width:60px"></label>
                    <label class="cfg-label" data-field="disp_f_byte">DISP_F_BYTE <input id="disp_f_byte" type="number" value="7" style="width:60px"></label>
                    <button onclick="saveProtocolConfig()">Save Config</button>
                    <button onclick="resetProtocolConfig()">Reset to Default</button>
                </div>
            </div>
        </div>
        <div class="card">
            <div class="row"><strong>Recent Packets</strong> <label class="muted"><input id="show_rx" type="checkbox" checked>rx</label> <label class="muted"><input id="show_tx" type="checkbox" checked>tx</label> <label class="muted"><input id="only_changed" type="checkbox">show changed only</label> <button onclick="clearBuffer()">Reset</button> <button onclick="refresh()">Refresh</button></div>
            <table><thead><tr><th>Age ms</th><th>Dir</th><th>Len</th><th>Data</th><th>Interpretation</th></tr></thead><tbody id="pkts"></tbody></table>
        </div>
    </div>
<script>
async function j(url,opt){const r=await fetch(url,opt);return r.json();}
function q(id){return document.getElementById(id)}
function b(v){return v?1:0}
function s(v){return v===null||v===undefined?"":String(v)}

const defaultProtocolConfig={
    pwr_byte:4,mode_byte:6,temp_set_lo_byte:10,temp_set_hi_byte:11,temp_act_byte:20,
    fan_spd1_byte:7,hswing_byte:8,vswing_byte:9,temp_set_byte:10,fan_spd2_byte:4,
    disp_on_byte:6,disp_mode_byte:9,disp_f_byte:7
};
const protocolConfig=Object.assign({},defaultProtocolConfig);

const protocolFieldColors={
    pwr_byte:'#ef4444',
    mode_byte:'#f59e0b',
    temp_set_lo_byte:'#10b981',
    temp_set_hi_byte:'#34d399',
    temp_act_byte:'#14b8a6',
    fan_spd1_byte:'#3b82f6',
    fan_spd2_byte:'#60a5fa',
    hswing_byte:'#8b5cf6',
    vswing_byte:'#ec4899',
    temp_set_byte:'#22c55e',
    disp_on_byte:'#f97316',
    disp_mode_byte:'#fb923c',
    disp_f_byte:'#facc15'
};

function hexToRgba(hex,alpha){
    const v=hex.replace('#','');
    const r=parseInt(v.slice(0,2),16);
    const g=parseInt(v.slice(2,4),16);
    const b=parseInt(v.slice(4,6),16);
    return `rgba(${r},${g},${b},${alpha})`;
}

function styleForFields(fields){
    const colors=fields.map(f=>protocolFieldColors[f]).filter(Boolean);
    if(colors.length===0) return '';
    if(colors.length===1){
        return `background:${hexToRgba(colors[0],0.22)};border-color:${colors[0]};`;
    }
    const step=100/colors.length;
    const stops=[];
    for(let i=0;i<colors.length;i++){
        const c=hexToRgba(colors[i],0.26);
        const start=(i*step).toFixed(2);
        const end=((i+1)*step).toFixed(2);
        stops.push(`${c} ${start}%`,`${c} ${end}%`);
    }
    return `background:linear-gradient(90deg,${stops.join(',')});border-color:${colors[0]};`;
}

function protocolByteMap(){
    const map={};
    Object.keys(protocolConfig).forEach(field=>{
        const idx=parseInt(protocolConfig[field]);
        if(Number.isNaN(idx)||idx<0) return;
        if(!map[idx]) map[idx]=[];
        map[idx].push(field);
    });
    return map;
}

function applyProtocolConfigColors(){
    const labels=document.querySelectorAll('#protocol_config label[data-field]');
    labels.forEach(label=>{
        const field=label.getAttribute('data-field');
        const style=styleForFields([field]);
        label.style.cssText=style;
    });
}

function loadProtocolConfig(){
    const saved=localStorage.getItem('protocolConfig');
    if(saved){
        const parsed=JSON.parse(saved);
        Object.assign(protocolConfig,parsed);
    }
    updateProtocolConfigUI();
}

function updateProtocolConfigUI(){
    Object.keys(protocolConfig).forEach(k=>{
        const el=q(k);
        if(el) el.value=protocolConfig[k];
    });
    applyProtocolConfigColors();
}

function saveProtocolConfig(){
    Object.keys(protocolConfig).forEach(k=>{
        const el=q(k);
        if(el) protocolConfig[k]=parseInt(el.value)||0;
    });
    localStorage.setItem('protocolConfig',JSON.stringify(protocolConfig));
    refresh();
}

function resetProtocolConfig(){
    Object.assign(protocolConfig,defaultProtocolConfig);
    localStorage.removeItem('protocolConfig');
    updateProtocolConfigUI();
    refresh();
}

function toggleProtocolConfig(){
    const el=q('protocol_config');
    el.style.display=el.style.display==='none'?'block':'none';
}

function hexToBytes(hex){
    const bytes=[];
    const str=hex.replace(/\s/g,'');
    for(let i=0;i<str.length;i+=2){
        bytes.push(parseInt(str.substr(i,2),16));
    }
    return bytes;
}

function renderHex(hex,dir){
    if(!hex) return '';
    const useHighlight=dir==='rx';
    const byteMap=useHighlight?protocolByteMap():{};
    return hex.split(' ').map((b,i)=>{
        const fields=byteMap[i]||[];
        const style=useHighlight?styleForFields(fields):'';
        const title=fields.length>0?`[${i}] 0x${b} | ${fields.join(', ')}`:`[${i}] 0x${b}`;
        return `<span class="hex-byte" style="${style}" title="${title}">${b}</span>`;
    }).join(' ');
}

function decodePacket(packet){
    try{
        if(!packet.hex) return '';
        const bytes=hexToBytes(packet.hex);
        if(bytes.length<4) return '(short)';
        const cmd=bytes[3];
        if(packet.dir==='tx'){
            if(cmd===0x01) return '[TX] Control';
            if(cmd===0x03) return '[TX] SyncTime';
            return `[TX] Cmd(${cmd.toString(16).toUpperCase()})`;
        }
        const minLen=Math.max(protocolConfig.pwr_byte,protocolConfig.mode_byte,protocolConfig.temp_set_lo_byte,protocolConfig.temp_set_hi_byte,protocolConfig.fan_spd1_byte,protocolConfig.hswing_byte,protocolConfig.vswing_byte)+1;
        if(cmd!==0x31||bytes.length<minLen) return `[RX] Pkt(${cmd.toString(16).toUpperCase()})`;
        const shortReport=cmd===0x31&&bytes.length===38;
        const pwr=shortReport?(bytes[19]&0x02)!==0:(bytes[protocolConfig.pwr_byte]&0x80)!==0;
        const mode=shortReport?bytes[protocolConfig.mode_byte]:(bytes[protocolConfig.mode_byte]&0x70)>>4;
        const modeMap=shortReport?{1:'cool',2:'dry',4:'fan',8:'heat'}:{1:'cool',2:'dry',3:'fan',4:'heat'};
        const tempSetRaw=bytes[protocolConfig.temp_set_lo_byte]+(bytes[protocolConfig.temp_set_hi_byte]<<8);
        const tempSet=16+((tempSetRaw-0x00A0)/10);
        let tempStr=tempSet.toFixed(1);
        if(bytes.length>protocolConfig.temp_act_byte){
            const tempAct=shortReport?bytes[protocolConfig.temp_act_byte]+4:(bytes[protocolConfig.temp_act_byte]-16)/2;
            tempStr+=`/${tempAct.toFixed(1)}`;
        }
        const fanSpd=bytes[protocolConfig.fan_spd1_byte]&0x07;
        const fanMap={0:'turbo',1:'auto',2:'low',4:'med',6:'high'};
        const hswing=bytes[protocolConfig.hswing_byte]&0x07;
        const hswingNames=['off','full','left','midl','mid','midr','right'];
        const vswing=(bytes[protocolConfig.vswing_byte]&0xf0)>>4;
        const vswingNames=['off','full','cup','cmidu','cmid','cmidd','cdown','down','midd','mid','midu','up'];
        const pwrSource=shortReport?` raw19=0x${bytes[19].toString(16).toUpperCase().padStart(2,'0')}`:'';
        return `[RX] pwr=${pwr?'ON':'OFF'}${pwrSource} mode=${modeMap[mode]||'keep'} temp=${tempStr} fan=${fanMap[fanSpd]||`unk(${fanSpd})`} hswing=${hswingNames[hswing]||hswing} vswing=${vswingNames[vswing]||vswing}`;
    }catch(e){
        return `(err:${e.message})`;
    }
}

const packetState={
    prevPackets:[],
    lastByDir:{},
    changedBuffer:[],
    onlyChangedActive:false,
    maxChanged:300
};

function packetKey(e){
    return `${e.dir}|${e.len}|${e.hex}`;
}

function overlapCount(prev,curr){
    const max=Math.min(prev.length,curr.length);
    for(let k=max;k>=0;k--){
        let ok=true;
        for(let i=0;i<k;i++){
            if(packetKey(prev[prev.length-k+i])!==packetKey(curr[i])){ok=false;break;}
        }
        if(ok) return k;
    }
    return 0;
}

function clearBuffer(){
    packetState.prevPackets=[];
    packetState.lastByDir={};
    packetState.changedBuffer=[];
    refresh();
}

async function refresh(){
    const st=await j('/api/status');
    q('ready').textContent=st.ready?'READY':'INITIALIZING';
    q('ready').className=st.ready?'ok':'bad';
    q('meta').textContent=`state=${st.state} update=${st.update} rx_age=${st.rx_age_ms} tx_age=${st.tx_age_ms}`;
    q('climate').textContent=`mode=${st.mode} target temp=${st.target_temperature} current temp=${st.current_temperature} fan=${st.fan}`;
    const p=await j('/api/packets');
    const packets=p.packets||[];
    const showRx=q('show_rx') && q('show_rx').checked;
    const showTx=q('show_tx') && q('show_tx').checked;
    const onlyChanged=q('only_changed') && q('only_changed').checked;

    const dirFiltered=packets.filter(e=>(showRx&&e.dir==='rx')||(showTx&&e.dir==='tx'));

    // When entering changed-only mode, reset browser-side history and start a fresh stream.
    if(onlyChanged && !packetState.onlyChangedActive){
        packetState.prevPackets=[];
        packetState.lastByDir={};
        packetState.changedBuffer=[];
    }
    packetState.onlyChangedActive=onlyChanged;

    let visible;
    if(onlyChanged){
        const overlap=overlapCount(packetState.prevPackets,packets);
        const newPackets=packets.slice(overlap);

        for(const e of newPackets){
            const prev=packetState.lastByDir[e.dir];
            const changed=!prev||e.hex!==prev.hex||e.len!==prev.len;
            packetState.lastByDir[e.dir]=e;
            if(changed){
                packetState.changedBuffer.push(e);
            }
        }

        if(packetState.changedBuffer.length>packetState.maxChanged){
            packetState.changedBuffer=packetState.changedBuffer.slice(packetState.changedBuffer.length-packetState.maxChanged);
        }

        visible=packetState.changedBuffer.filter(e=>(showRx&&e.dir==='rx')||(showTx&&e.dir==='tx'));
    }else{
        visible=dirFiltered;
    }

    packetState.prevPackets=packets;
    q('pkts').innerHTML=visible.map(e=>`<tr><td>${e.age_ms}</td><td>${e.dir}</td><td>${e.len}</td><td class="hex">${renderHex(e.hex,e.dir)}</td><td>${decodePacket(e)}</td></tr>`).join('');
}
async function sendRaw(){
    const body=new URLSearchParams({hex:q('hex').value});
    const r=await j('/api/raw',{method:'POST',headers:{'content-type':'application/x-www-form-urlencoded'},body});
    q('raw_result').textContent=r.ok?'sent':'error: '+(r.error||'unknown');
    refresh();
}
async function sendControl(){
    const body=new URLSearchParams({
        power:s(q('power').value),mode:s(q('mode').value),temp:s(q('temp').value),fan:s(q('fan').value),
        vswing:s(q('vswing').value),hswing:s(q('hswing').value),display:s(q('display').value),unit:s(q('unit').value),
        plasma:b(q('plasma').checked),sleep:b(q('sleep').checked),xfan:b(q('xfan').checked),save:b(q('save').checked)
    });
    const r=await j('/api/control',{method:'POST',headers:{'content-type':'application/x-www-form-urlencoded'},body});
    q('control_result').textContent=r.ok?'queued':'error: '+(r.error||'unknown');
    refresh();
}
loadProtocolConfig();
setInterval(refresh, 600);
refresh();
</script>
</body>
</html>
)HTML";

SinclairACCNT::SinclairACCNT() {}

std::string SinclairACCNT::json_escape_(const std::string &value)
{
        std::string out;
        out.reserve(value.size() + 8);
        for (char ch : value)
        {
                if (ch == '\\' || ch == '"')
                {
                        out.push_back('\\');
                        out.push_back(ch);
                }
                else if (ch == '\n')
                {
                        out += "\\n";
                }
                else if (ch == '\r')
                {
                        out += "\\r";
                }
                else
                {
                        out.push_back(ch);
                }
        }
        return out;
}

std::string SinclairACCNT::packet_to_hex_(const uint8_t *data, size_t len)
{
        static const char *HEX_DIGITS = "0123456789ABCDEF";
        std::string out;
        out.reserve(len * 3);
        for (size_t i = 0; i < len; i++)
        {
                uint8_t b = data[i];
                out.push_back(HEX_DIGITS[(b >> 4) & 0x0F]);
                out.push_back(HEX_DIGITS[b & 0x0F]);
                if (i + 1 < len)
                        out.push_back(' ');
        }
        return out;
}

void SinclairACCNT::record_debug_packet_(const std::vector<uint8_t> &packet, bool outgoing)
{
        DebugPacket &entry = this->debug_packets_[this->debug_packet_head_];
        entry.timestamp_ms = millis();
        entry.outgoing = outgoing;
        entry.len = std::min(packet.size(), static_cast<size_t>(DATA_MAX));
        std::fill(entry.bytes.begin(), entry.bytes.end(), 0);
        if (entry.len > 0)
        {
                std::copy(packet.begin(), packet.begin() + entry.len, entry.bytes.begin());
        }

        this->debug_packet_head_ = (this->debug_packet_head_ + 1) % DEBUG_PACKET_HISTORY_SIZE;
        if (this->debug_packet_count_ < DEBUG_PACKET_HISTORY_SIZE)
        {
                this->debug_packet_count_++;
        }
}

bool SinclairACCNT::parse_hex_packet_(const std::string &hex_input, std::vector<uint8_t> *packet)
{
        packet->clear();
        int nibble = -1;
        for (char ch : hex_input)
        {
                if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',' || ch == ':')
                        continue;

                int val = -1;
                if (ch >= '0' && ch <= '9')
                        val = ch - '0';
                else if (ch >= 'a' && ch <= 'f')
                        val = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F')
                        val = ch - 'A' + 10;
                else
                        return false;

                if (nibble < 0)
                {
                        nibble = val;
                }
                else
                {
                        packet->push_back(static_cast<uint8_t>((nibble << 4) | val));
                        nibble = -1;
                        if (packet->size() >= DATA_MAX)
                                break;
                }
        }
        return nibble < 0 && !packet->empty();
}

std::string SinclairACCNT::json_status_()
{
    const uint32_t now = millis();
    const char *state = this->state_ == ACState::Ready ? "ready" : "initializing";
    const char *update = "none";
    if (this->update_ == ACUpdate::UpdateStart)
        update = "update_start";
    else if (this->update_ == ACUpdate::UpdateClear)
        update = "update_clear";

    std::string fan = fan_modes::FAN_AUTO;
    if (this->has_custom_fan_mode())
        fan = this->get_custom_fan_mode().c_str();

    std::string mode = "off";
    switch (this->mode)
    {
        case climate::CLIMATE_MODE_AUTO: mode = "auto"; break;
        case climate::CLIMATE_MODE_COOL: mode = "cool"; break;
        case climate::CLIMATE_MODE_HEAT: mode = "heat"; break;
        case climate::CLIMATE_MODE_DRY: mode = "dry"; break;
        case climate::CLIMATE_MODE_FAN_ONLY: mode = "fan_only"; break;
        default: mode = "off"; break;
    }

    std::string out = "{";
    out += "\"ready\":" + std::string(this->state_ == ACState::Ready ? "true" : "false");
    out += ",\"state\":\"" + std::string(state) + "\"";
    out += ",\"update\":\"" + std::string(update) + "\"";
    out += ",\"power\":" + std::string(this->power_internal_ ? "true" : "false");
    out += ",\"mode\":\"" + mode + "\"";
    out += ",\"fan\":\"" + this->json_escape_(fan) + "\"";
    out += ",\"target_temperature\":";
    out += std::isfinite(this->target_temperature) ? std::to_string(this->target_temperature) : "null";
    out += ",\"current_temperature\":";
    out += std::isfinite(this->current_temperature) ? std::to_string(this->current_temperature) : "null";
    out += ",\"rx_age_ms\":" + std::to_string(now - this->last_packet_received_);
    out += ",\"tx_age_ms\":" + std::to_string(now - this->last_packet_sent_);
    out += "}";
    return out;
}

std::string SinclairACCNT::json_packets_(uint8_t max_packets)
{
    const uint32_t now = millis();
    std::string out = "{\"packets\":[";
    const uint8_t packet_count = std::min(this->debug_packet_count_, max_packets);
    out.reserve(16 + packet_count * 180);
    for (uint8_t i = 0; i < packet_count; i++)
    {
        App.feed_wdt();
        const uint8_t idx = (this->debug_packet_head_ + DEBUG_PACKET_HISTORY_SIZE - packet_count + i) % DEBUG_PACKET_HISTORY_SIZE;
        const DebugPacket &entry = this->debug_packets_[idx];
        if (i > 0)
            out += ",";
        out += "{";
        out += "\"age_ms\":" + std::to_string(now - entry.timestamp_ms);
        out += ",\"dir\":\"" + std::string(entry.outgoing ? "tx" : "rx") + "\"";
        out += ",\"len\":" + std::to_string(entry.len);
        out += ",\"hex\":\"" + this->packet_to_hex_(entry.bytes.data(), entry.len) + "\"";
        out += "}";
    }
    out += "]}";
    return out;
}

bool SinclairACCNT::apply_debug_control_()
{
    bool changed = false;

    if (this->debug_server_->hasArg("power"))
    {
        const auto value = this->debug_server_->arg("power");
        if (value == "off")
        {
            this->mode = climate::CLIMATE_MODE_OFF;
            changed = true;
        }
        else if (value == "on" && this->mode == climate::CLIMATE_MODE_OFF)
        {
            this->mode = climate::CLIMATE_MODE_COOL;
            changed = true;
        }
    }

    if (this->debug_server_->hasArg("mode"))
    {
        const auto value = this->debug_server_->arg("mode");
        if (value == "cool") { this->mode = climate::CLIMATE_MODE_COOL; changed = true; }
        else if (value == "dry") { this->mode = climate::CLIMATE_MODE_DRY; changed = true; }
        else if (value == "fan_only") { this->mode = climate::CLIMATE_MODE_FAN_ONLY; changed = true; }
    }

    if (this->debug_server_->hasArg("temp"))
    {
        const auto arg = this->debug_server_->arg("temp");
        if (arg.length() > 0)
        {
            float temp = static_cast<float>(atof(arg.c_str()));
            temp = std::max(static_cast<float>(MIN_TEMPERATURE), std::min(static_cast<float>(MAX_TEMPERATURE), temp));
            this->target_temperature = temp;
            changed = true;
        }
    }

    if (this->debug_server_->hasArg("fan"))
    {
        const auto value = this->debug_server_->arg("fan");
        if (value == "auto") { this->set_custom_fan_mode_(fan_modes::FAN_AUTO); changed = true; }
        else if (value == "quiet") { this->set_custom_fan_mode_(fan_modes::FAN_QUIET); changed = true; }
        else if (value == "low") { this->set_custom_fan_mode_(fan_modes::FAN_LOW); changed = true; }
        else if (value == "medl") { this->set_custom_fan_mode_(fan_modes::FAN_MEDL); changed = true; }
        else if (value == "med") { this->set_custom_fan_mode_(fan_modes::FAN_MED); changed = true; }
        else if (value == "medh") { this->set_custom_fan_mode_(fan_modes::FAN_MEDH); changed = true; }
        else if (value == "high") { this->set_custom_fan_mode_(fan_modes::FAN_HIGH); changed = true; }
        else if (value == "turbo") { this->set_custom_fan_mode_(fan_modes::FAN_TURBO); changed = true; }
    }

    if (this->debug_server_->hasArg("vswing"))
    {
        const auto value = this->debug_server_->arg("vswing");
        if (value == "off") { this->vertical_swing_state_ = vertical_swing_options::OFF; changed = true; }
        else if (value == "full") { this->vertical_swing_state_ = vertical_swing_options::FULL; changed = true; }
        else if (value == "up") { this->vertical_swing_state_ = vertical_swing_options::UP; changed = true; }
        else if (value == "mid") { this->vertical_swing_state_ = vertical_swing_options::MID; changed = true; }
        else if (value == "down") { this->vertical_swing_state_ = vertical_swing_options::DOWN; changed = true; }
    }

    if (this->debug_server_->hasArg("hswing"))
    {
        const auto value = this->debug_server_->arg("hswing");
        if (value == "off") { this->horizontal_swing_state_ = horizontal_swing_options::OFF; changed = true; }
        else if (value == "full") { this->horizontal_swing_state_ = horizontal_swing_options::FULL; changed = true; }
        else if (value == "left") { this->horizontal_swing_state_ = horizontal_swing_options::CLEFT; changed = true; }
        else if (value == "mid") { this->horizontal_swing_state_ = horizontal_swing_options::CMID; changed = true; }
        else if (value == "right") { this->horizontal_swing_state_ = horizontal_swing_options::CRIGHT; changed = true; }
    }

    if (this->debug_server_->hasArg("display"))
    {
        const auto value = this->debug_server_->arg("display");
        if (value == "off") { this->display_state_ = display_options::OFF; changed = true; }
        else if (value == "auto") { this->display_state_ = display_options::AUTO; changed = true; }
        else if (value == "set") { this->display_state_ = display_options::SET; changed = true; }
        else if (value == "act") { this->display_state_ = display_options::ACT; changed = true; }
        else if (value == "out") { this->display_state_ = display_options::OUT; changed = true; }
    }

    if (this->debug_server_->hasArg("unit"))
    {
        const auto value = this->debug_server_->arg("unit");
        if (value == "c") { this->display_unit_state_ = display_unit_options::DEGC; changed = true; }
        else if (value == "f") { this->display_unit_state_ = display_unit_options::DEGF; changed = true; }
    }

    if (this->debug_server_->hasArg("plasma")) { this->plasma_state_ = this->debug_server_->arg("plasma") == "1"; changed = true; }
    if (this->debug_server_->hasArg("sleep")) { this->sleep_state_ = this->debug_server_->arg("sleep") == "1"; changed = true; }
    if (this->debug_server_->hasArg("xfan")) { this->xfan_state_ = this->debug_server_->arg("xfan") == "1"; changed = true; }
    if (this->debug_server_->hasArg("save")) { this->save_state_ = this->debug_server_->arg("save") == "1"; changed = true; }

    if (changed)
    {
        this->update_ = ACUpdate::UpdateStart;
    }

    return changed;
}

void SinclairACCNT::handle_debug_root_()
{
    this->debug_server_->send(200, "text/html", DEBUG_UI_HTML);
}

void SinclairACCNT::handle_debug_status_()
{
    this->debug_server_->send(200, "application/json", this->json_status_().c_str());
}

void SinclairACCNT::handle_debug_packets_()
{
    uint8_t limit = DEBUG_PACKET_RESPONSE_SIZE;
    if (this->debug_server_->hasArg("limit"))
    {
        int parsed_limit = atoi(this->debug_server_->arg("limit").c_str());
        if (parsed_limit > 0)
        {
            limit = std::min(static_cast<uint8_t>(parsed_limit), DEBUG_PACKET_HISTORY_SIZE);
        }
    }

    this->debug_server_->send(200, "application/json", this->json_packets_(limit).c_str());
}

void SinclairACCNT::handle_debug_raw_send_()
{
    if (!this->debug_server_->hasArg("hex"))
    {
        this->debug_server_->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing hex argument\"}");
        return;
    }

    const uint32_t now = millis();
    if (now - this->last_debug_raw_sent_ < protocol::TIME_REFRESH_PERIOD_MS ||
        now - this->last_packet_sent_ < protocol::TIME_REFRESH_PERIOD_MS)
    {
        this->debug_server_->send(429, "application/json", "{\"ok\":false,\"error\":\"Rate limited\"}");
        return;
    }

    std::vector<uint8_t> packet;
    if (!this->parse_hex_packet_(this->debug_server_->arg("hex").c_str(), &packet))
    {
        this->debug_server_->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid hex payload\"}");
        return;
    }

    this->write_array(packet);
    this->last_debug_raw_sent_ = now;
    this->last_packet_sent_ = this->last_debug_raw_sent_;
    this->wait_response_ = true;
    log_packet(packet, true);
    this->record_debug_packet_(packet, true);
    this->debug_server_->send(200, "application/json", "{\"ok\":true}");
}

void SinclairACCNT::handle_debug_control_()
{
    if (this->state_ != ACState::Ready)
    {
        this->debug_server_->send(409, "application/json", "{\"ok\":false,\"error\":\"AC not ready\"}");
        return;
    }

    if (!this->apply_debug_control_())
    {
        this->debug_server_->send(400, "application/json", "{\"ok\":false,\"error\":\"No valid control args\"}");
        return;
    }
    this->debug_server_->send(200, "application/json", "{\"ok\":true}");
}

void SinclairACCNT::init_debug_server_()
{
    if (!this->debug_ui_enabled_ || this->debug_ui_ready_)
        return;

#if defined(USE_ESP8266)
    this->debug_server_ = new ESP8266WebServer(this->debug_ui_port_);
#elif defined(USE_ESP32)
    this->debug_server_ = new WebServer(this->debug_ui_port_);
#else
    return;
#endif

    this->debug_server_->on("/", [this]() { this->handle_debug_root_(); });
    this->debug_server_->on("/api/status", [this]() { this->handle_debug_status_(); });
    this->debug_server_->on("/api/packets", [this]() { this->handle_debug_packets_(); });
    this->debug_server_->on("/api/raw", HTTP_POST, [this]() { this->handle_debug_raw_send_(); });
    this->debug_server_->on("/api/control", HTTP_POST, [this]() { this->handle_debug_control_(); });
    this->debug_server_->onNotFound([this]() { this->debug_server_->send(404, "text/plain", "Not found"); });
    this->debug_server_->begin();
    this->debug_ui_ready_ = true;

    ESP_LOGI(TAG, "Debug UI enabled at port %u", this->debug_ui_port_);
}

void SinclairACCNT::handle_debug_server_()
{
    if (!this->debug_ui_enabled_)
        return;

    if (!this->debug_ui_ready_)
        this->init_debug_server_();

    if (this->debug_server_ != nullptr)
        this->debug_server_->handleClient();
}

void SinclairACCNT::setup()
{
    SinclairAC::setup();
    this->init_debug_server_();

    ESP_LOGD(TAG, "Using serial protocol for Sinclair AC");
}

void SinclairACCNT::loop()
{
    this->handle_debug_server_();
    /* this reads data from UART */
    SinclairAC::loop();

    /* we have a frame from AC */
    if (this->serialProcess_.state == STATE_COMPLETE)
    {
        /* do not forget to order for restart of the recieve state machine */
        this->serialProcess_.state = STATE_RESTART;
        /* mark that we have recieved a response */
        this->wait_response_ = false;
        /* log for ESPHome debug */
        log_packet(this->serialProcess_.data);
        this->record_debug_packet_(this->serialProcess_.data, false);

        if (!verify_packet())  /* Verify length, header, counter and checksum */
        {
            return;
        }

        this->last_packet_received_ = millis();  /* Set the time at which we received our last packet */

        /* A valid recieved packet of accepted type marks module as being ready */
        if (this->state_ != ACState::Ready)
        {
            this->state_ = ACState::Ready;  
            Component::status_clear_error();
            this->last_packet_sent_ = millis();
        }

        if (this->update_ == ACUpdate::NoUpdate)
        {
            handle_packet(); /* this will update state of components in HA as well as internal settings */
        }
    }

    /* we will send a packet to the AC as a reponse to indicate changes */
    send_packet();

    /* if there are no packets for a while - mark module as not ready */
    if (millis() - this->last_packet_received_ >= protocol::TIME_TIMEOUT_INACTIVE_MS)
    {
        if (this->state_ != ACState::Initializing)
        {
            this->state_ = ACState::Initializing;
            Component::status_set_error();
        }
    }
}

/*
 * ESPHome control request
 */

void SinclairACCNT::control(const climate::ClimateCall &call)
{
    if (this->state_ != ACState::Ready)
        return;

    if (call.get_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested mode change");
        this->update_ = ACUpdate::UpdateStart;
        climate::ClimateMode requested_mode = *call.get_mode();
        if (requested_mode == climate::CLIMATE_MODE_AUTO ||
            requested_mode == climate::CLIMATE_MODE_HEAT ||
            requested_mode == climate::CLIMATE_MODE_HEAT_COOL)
        {
            requested_mode = climate::CLIMATE_MODE_COOL;
        }
        this->mode = requested_mode;
    }

    if (call.get_target_temperature().has_value())
    {
        ESP_LOGV(TAG, "Requested target teperature change");
        this->update_ = ACUpdate::UpdateStart;
        this->target_temperature = *call.get_target_temperature();
        if (this->target_temperature < MIN_TEMPERATURE)
        {
            this->target_temperature = MIN_TEMPERATURE;
        }
        else if (this->target_temperature > MAX_TEMPERATURE)
        {
            this->target_temperature = MAX_TEMPERATURE;
        }
    }

    if (call.has_custom_fan_mode())
    {
        ESP_LOGV(TAG, "Requested fan mode change");
        this->update_ = ACUpdate::UpdateStart;
        this->set_custom_fan_mode_(call.get_custom_fan_mode());
    }

    if (call.get_swing_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested swing mode change");
        this->update_ = ACUpdate::UpdateStart;
        switch (*call.get_swing_mode()) {
            case climate::CLIMATE_SWING_BOTH:
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            case climate::CLIMATE_SWING_OFF:
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_VERTICAL:
                /* vertical full, horizontal center */
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_HORIZONTAL:
                /* horizontal full, vertical center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            default:
                ESP_LOGV(TAG, "Unsupported swing mode requested");
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
        }
    }
}

/*
 * Send a raw packet, as is
 */

void SinclairACCNT::send_short_control_packet_(uint32_t now)
{
    std::vector<uint8_t> packet(protocol::SET_SHORT_PACKET_LEN, 0);

    const bool power = this->mode != climate::CLIMATE_MODE_OFF;

    uint8_t mode = protocol::REPORT_SHORT_MODE_COOL;
    climate::ClimateMode requested_mode = power ? this->mode : this->mode_internal_;
    switch (requested_mode)
    {
        case climate::CLIMATE_MODE_COOL:
            mode = protocol::REPORT_SHORT_MODE_COOL;
            break;
        case climate::CLIMATE_MODE_DRY:
            mode = protocol::REPORT_SHORT_MODE_DRY;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            mode = protocol::REPORT_SHORT_MODE_FAN;
            break;
        default:
            mode = protocol::REPORT_SHORT_MODE_COOL;
            break;
    }

    uint8_t fanSpeed1 = 1;
    if (this->has_custom_fan_mode())
    {
        const char* custom_fan_mode = this->get_custom_fan_mode().c_str();
        if (strcmp(custom_fan_mode, fan_modes::FAN_TURBO) == 0)
            fanSpeed1 = 0;
        else if (strcmp(custom_fan_mode, fan_modes::FAN_LOW) == 0 || strcmp(custom_fan_mode, fan_modes::FAN_QUIET) == 0)
            fanSpeed1 = 2;
        else if (strcmp(custom_fan_mode, fan_modes::FAN_MEDL) == 0 || strcmp(custom_fan_mode, fan_modes::FAN_MED) == 0 || strcmp(custom_fan_mode, fan_modes::FAN_MEDH) == 0)
            fanSpeed1 = 4;
        else if (strcmp(custom_fan_mode, fan_modes::FAN_HIGH) == 0)
            fanSpeed1 = 6;
    }

    uint16_t target_temperature_raw = protocol::REPORT_TEMP_SET_RAW_BASE;
    if (this->target_temperature > protocol::REPORT_TEMP_SET_C_BASE)
    {
        target_temperature_raw += static_cast<uint16_t>(
            lround((this->target_temperature - protocol::REPORT_TEMP_SET_C_BASE) * protocol::REPORT_TEMP_SET_RAW_STEP));
    }

    uint8_t mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    if (this->vertical_swing_state_ == vertical_swing_options::FULL)
        mode_vertical_swing = protocol::REPORT_VSWING_FULL;
    else if (this->vertical_swing_state_ == vertical_swing_options::DOWN)
        mode_vertical_swing = protocol::REPORT_VSWING_DOWN;
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDD)
        mode_vertical_swing = protocol::REPORT_VSWING_MIDD;
    else if (this->vertical_swing_state_ == vertical_swing_options::MID)
        mode_vertical_swing = protocol::REPORT_VSWING_MID;
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDU)
        mode_vertical_swing = protocol::REPORT_VSWING_MIDU;
    else if (this->vertical_swing_state_ == vertical_swing_options::UP)
        mode_vertical_swing = protocol::REPORT_VSWING_UP;
    else if (this->vertical_swing_state_ == vertical_swing_options::CDOWN)
        mode_vertical_swing = protocol::REPORT_VSWING_CDOWN;
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDD)
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDD;
    else if (this->vertical_swing_state_ == vertical_swing_options::CMID)
        mode_vertical_swing = protocol::REPORT_VSWING_CMID;
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDU)
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDU;
    else if (this->vertical_swing_state_ == vertical_swing_options::CUP)
        mode_vertical_swing = protocol::REPORT_VSWING_CUP;

    uint8_t mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    if (this->horizontal_swing_state_ == horizontal_swing_options::FULL)
        mode_horizontal_swing = protocol::REPORT_HSWING_FULL;
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CLEFT)
        mode_horizontal_swing = protocol::REPORT_HSWING_CLEFT;
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDL)
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDL;
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMID)
        mode_horizontal_swing = protocol::REPORT_HSWING_CMID;
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDR)
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDR;
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CRIGHT)
        mode_horizontal_swing = protocol::REPORT_HSWING_CRIGHT;

    uint8_t current_temperature_raw = 0x11;
    if (!std::isnan(this->current_temperature))
    {
        const int raw = static_cast<int>(lround(this->current_temperature - protocol::REPORT_SHORT_TEMP_ACT_OFF));
        current_temperature_raw = static_cast<uint8_t>(std::max(0, std::min(255, raw)));
    }

    packet[protocol::SET_SHORT_TRANSITION_BYTE] = protocol::SET_SHORT_TRANSITION_VAL;
    packet[protocol::REPORT_SHORT_MODE_BYTE] = mode;
    packet[protocol::REPORT_SHORT_FAN_SPD1_BYTE] = fanSpeed1;
    packet[protocol::REPORT_SHORT_HSWING_BYTE] = mode_horizontal_swing;
    packet[protocol::REPORT_SHORT_VSWING_BYTE] = (mode_vertical_swing << protocol::REPORT_VSWING_POS) | protocol::SET_SHORT_VSWING_CONST_MASK;
    packet[protocol::REPORT_SHORT_TEMP_SET_LO_BYTE] = static_cast<uint8_t>(target_temperature_raw & 0xFF);
    if ((target_temperature_raw & 0x100) != 0)
        packet[protocol::REPORT_SHORT_TEMP_SET_HI_BYTE] |= protocol::REPORT_TEMP_SET_HI_MASK;
    packet[protocol::REPORT_SHORT_PWR_BYTE] = protocol::REPORT_SHORT_PWR_BASE;
    if (power)
        packet[protocol::REPORT_SHORT_PWR_BYTE] |= protocol::REPORT_SHORT_PWR_MASK;
    packet[protocol::REPORT_SHORT_TEMP_ACT_BYTE] = current_temperature_raw;

    packet.insert(packet.begin(), protocol::CMD_OUT_PARAMS_SET);
    packet.insert(packet.begin(), protocol::SET_SHORT_PACKET_LEN + 2);

    uint8_t checksum = 0;
    for (uint8_t i = 0 ; i < packet.size() ; i++)
    {
        checksum += packet[i];
    }
    packet.push_back(checksum);

    packet.insert(packet.begin(), protocol::SYNC);
    packet.insert(packet.begin(), protocol::SYNC);

    this->last_packet_sent_ = now;
    this->wait_response_ = true;
    write_array(packet);
    log_packet(packet, true);
    this->record_debug_packet_(packet, true);
    this->update_ = ACUpdate::NoUpdate;
}

void SinclairACCNT::send_packet()
{
    std::vector<uint8_t> packet(protocol::SET_PACKET_LEN, 0);  /* Initialize packet contents */
    const uint32_t now = millis();
    const bool has_update = this->update_ != ACUpdate::NoUpdate;

    if (now - this->last_packet_sent_ < protocol::TIME_REFRESH_PERIOD_MS)
    {
        /* do not send command/clear frames too quickly */
        return;
    }
    if (this->wait_response_ && !has_update)
    {
        /* wait for the last idle poll response before sending another idle poll */
        return;
    }
    if (!has_update && (now - this->last_packet_sent_) < protocol::TIME_IDLE_POLL_PERIOD_MS)
    {
        /* keep idle polling slow so it cannot immediately wash out a command */
        return;
    }

    const bool power_changed = (this->mode != climate::CLIMATE_MODE_OFF) != this->power_internal_;
    const bool target_temperature_changed = this->target_temperature_reported_ < 0.0f ||
        std::fabs(this->target_temperature - this->target_temperature_reported_) >= 0.1f;
    if (this->update_ == ACUpdate::UpdateStart && (power_changed || target_temperature_changed))
    {
        this->send_short_control_packet_(now);
        return;
    }
    
    packet[protocol::SET_CONST_02_BYTE] = protocol::SET_CONST_02_VAL; /* Some always 0x02 byte... */
    packet[protocol::SET_CONST_BIT_BYTE] = protocol::SET_CONST_BIT_MASK; /* Some always true bit */

    /* Prepare the rest of the frame */
    /* this handles tricky part of 0xAF value and flag marking that WiFi does not apply any changes */
    switch(this->update_)
    {
        default:
        case ACUpdate::NoUpdate:
            packet[protocol::SET_NOCHANGE_BYTE] |= protocol::SET_NOCHANGE_MASK;
            break;
        case ACUpdate::UpdateStart:
            packet[protocol::SET_AF_BYTE] = protocol::SET_AF_VAL;
            break;
        case ACUpdate::UpdateClear:
            break;
    }

    /* MODE and POWER --------------------------------------------------------------------------- */
    uint8_t mode = protocol::REPORT_MODE_AUTO;
    bool power = false;
    switch (this->mode)
    {
        case climate::CLIMATE_MODE_AUTO:
            /* This model has no explicit AUTO code; keep current concrete mode while powering on. */
            switch (this->mode_internal_)
            {
                case climate::CLIMATE_MODE_COOL:
                    mode = protocol::REPORT_MODE_COOL;
                    break;
                case climate::CLIMATE_MODE_DRY:
                    mode = protocol::REPORT_MODE_DRY;
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    mode = protocol::REPORT_MODE_FAN;
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    mode = protocol::REPORT_MODE_HEAT;
                    break;
                case climate::CLIMATE_MODE_AUTO:
                case climate::CLIMATE_MODE_OFF:
                case climate::CLIMATE_MODE_HEAT_COOL:
                default:
                    mode = protocol::REPORT_MODE_COOL;
                    break;
            }
            power = true;
            break;
        case climate::CLIMATE_MODE_COOL:
            mode = protocol::REPORT_MODE_COOL;
            power = true;
            break;
        case climate::CLIMATE_MODE_DRY:
            mode = protocol::REPORT_MODE_DRY;
            power = true;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            mode = protocol::REPORT_MODE_FAN;
            power = true;
            break;
        case climate::CLIMATE_MODE_HEAT:
            mode = protocol::REPORT_MODE_HEAT;
            power = true;
            break;
        default:
        case climate::CLIMATE_MODE_OFF:
            /* In case of MODE_OFF we will not alter the last mode setting recieved from AC, see determine_mode() */
            switch (this->mode_internal_)
            {
                case climate::CLIMATE_MODE_AUTO:
                    mode = protocol::REPORT_MODE_AUTO;
                    break;
                case climate::CLIMATE_MODE_COOL:
                    mode = protocol::REPORT_MODE_COOL;
                    break;
                case climate::CLIMATE_MODE_DRY:
                    mode = protocol::REPORT_MODE_DRY;
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    mode = protocol::REPORT_MODE_FAN;
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    mode = protocol::REPORT_MODE_HEAT;
                    break;
                case climate::CLIMATE_MODE_OFF:
                case climate::CLIMATE_MODE_HEAT_COOL:
                default:
                    break;
            }
            power = false;
            break;
    }

    packet[protocol::REPORT_MODE_BYTE] |= (mode << protocol::REPORT_MODE_POS);
    if (power)
    {
        packet[protocol::REPORT_PWR_BYTE] |= protocol::REPORT_PWR_MASK;
    }

    /* TARGET TEMPERATURE --------------------------------------------------------------------------- */
    uint16_t target_temperature_raw = protocol::REPORT_TEMP_SET_RAW_BASE;
    if (this->target_temperature > protocol::REPORT_TEMP_SET_C_BASE)
    {
        target_temperature_raw += static_cast<uint16_t>(
            lround((this->target_temperature - protocol::REPORT_TEMP_SET_C_BASE) * protocol::REPORT_TEMP_SET_RAW_STEP));
    }
    packet[protocol::REPORT_TEMP_SET_LO_BYTE] = static_cast<uint8_t>(target_temperature_raw & 0xFF);
    if ((target_temperature_raw & 0x100) != 0)
    {
        packet[protocol::REPORT_TEMP_SET_HI_BYTE] |= protocol::REPORT_TEMP_SET_HI_MASK;
    }
    else
    {
        packet[protocol::REPORT_TEMP_SET_HI_BYTE] &= ~protocol::REPORT_TEMP_SET_HI_MASK;
    }

    /* FAN SPEED --------------------------------------------------------------------------- */
    /* below will default to AUTO */
    uint8_t fanSpeed1 = 1;
    uint8_t fanSpeed2 = 0;
    bool    fanQuiet  = false;
    bool    fanTurbo  = false;
    if (this->has_custom_fan_mode())
    {
        const char* custom_fan_mode = this->get_custom_fan_mode().c_str();

        if (strcmp(custom_fan_mode, fan_modes::FAN_AUTO) == 0)
        {
            fanSpeed1 = 1;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_LOW) == 0)
        {
            fanSpeed1 = 2;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_QUIET) == 0)
        {
            fanSpeed1 = 2;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_MEDL) == 0)
        {
            fanSpeed1 = 4;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_MED) == 0)
        {
            fanSpeed1 = 4;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_MEDH) == 0)
        {
            fanSpeed1 = 4;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_HIGH) == 0)
        {
            fanSpeed1 = 6;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_TURBO) == 0)
        {
            fanSpeed1 = 0;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
        else
        {
            fanSpeed1 = 1;
            fanSpeed2 = 0;
            fanQuiet  = false;
            fanTurbo  = false;
        }
    }

    packet[protocol::REPORT_FAN_SPD1_BYTE] |= (fanSpeed1 << protocol::REPORT_FAN_SPD1_POS);
    packet[protocol::REPORT_FAN_SPD2_BYTE] |= (fanSpeed2 << protocol::REPORT_FAN_SPD2_POS);
    if (fanTurbo)
    {
        packet[protocol::REPORT_FAN_TURBO_BYTE] |= protocol::REPORT_FAN_TURBO_MASK;
    }
    if (fanQuiet)
    {
        packet[protocol::REPORT_FAN_QUIET_BYTE] |= protocol::REPORT_FAN_QUIET_MASK;
    }

    /* VERTICAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    if (this->vertical_swing_state_ == vertical_swing_options::OFF)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::FULL)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_FULL;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::DOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_DOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::UP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_UP;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CDOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CDOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CUP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CUP;
    }
    else
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    packet[protocol::REPORT_VSWING_BYTE] |= (mode_vertical_swing << protocol::REPORT_VSWING_POS);

    /* HORIZONTAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    if (this->horizontal_swing_state_ == horizontal_swing_options::OFF)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::FULL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_FULL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CLEFT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CLEFT;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMID)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMID;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDR)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDR;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CRIGHT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CRIGHT;
    }
    else
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    packet[protocol::REPORT_HSWING_BYTE] |= (mode_horizontal_swing << protocol::REPORT_HSWING_POS);

    /* DISPLAY --------------------------------------------------------------------------- */
    uint8_t display_mode = protocol::REPORT_DISP_MODE_AUTO;
    if (this->display_state_ == display_options::AUTO)
    {
        display_mode = protocol::REPORT_DISP_MODE_AUTO;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::SET)
    {
        display_mode = protocol::REPORT_DISP_MODE_SET;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::ACT)
    {
        display_mode = protocol::REPORT_DISP_MODE_ACT;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::OUT)
    {
        display_mode = protocol::REPORT_DISP_MODE_OUT;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::OFF)
    {
        /* we do not want to alter display setting - only turn it off */
        this->display_power_internal_ = false;
        if (this->display_mode_internal_ == display_options::AUTO)
        {
            display_mode = protocol::REPORT_DISP_MODE_AUTO;
        }
        else if (this->display_mode_internal_ == display_options::SET)
        {
            display_mode = protocol::REPORT_DISP_MODE_SET;
        }
        else if (this->display_mode_internal_ == display_options::ACT)
        {
            display_mode = protocol::REPORT_DISP_MODE_ACT;
        }
        else if (this->display_mode_internal_ == display_options::OUT)
        {
            display_mode = protocol::REPORT_DISP_MODE_OUT;
        }
        else
        {
            display_mode = protocol::REPORT_DISP_MODE_AUTO;
        }
    }
    else
    {
        display_mode = protocol::REPORT_DISP_MODE_AUTO;
        this->display_power_internal_ = true;
    }

    packet[protocol::REPORT_DISP_MODE_BYTE] |= (display_mode << protocol::REPORT_DISP_MODE_POS);

    if (this->display_power_internal_)
    {
        packet[protocol::REPORT_DISP_ON_BYTE] |= protocol::REPORT_DISP_ON_MASK;
    }

    /* DISPLAY UNIT --------------------------------------------------------------------------- */
    if (this->display_unit_state_ == display_unit_options::DEGF)
    {
        packet[protocol::REPORT_DISP_F_BYTE] |= protocol::REPORT_DISP_F_MASK;
    }

    /* PLASMA --------------------------------------------------------------------------- */
    if (this->plasma_state_)
    {
        packet[protocol::REPORT_PLASMA1_BYTE] |= protocol::REPORT_PLASMA1_MASK;
        packet[protocol::REPORT_PLASMA2_BYTE] |= protocol::REPORT_PLASMA2_MASK;
    }

    /* SLEEP --------------------------------------------------------------------------- */
    if (this->sleep_state_)
    {
        packet[protocol::REPORT_SLEEP_BYTE] |= protocol::REPORT_SLEEP_MASK;
    }

    /* XFAN --------------------------------------------------------------------------- */
    if (this->xfan_state_)
    {
        packet[protocol::REPORT_XFAN_BYTE] |= protocol::REPORT_XFAN_MASK;
    }

    /* SAVE --------------------------------------------------------------------------- */
    if (this->save_state_)
    {
        packet[protocol::REPORT_SAVE_BYTE] |= protocol::REPORT_SAVE_MASK;
    }
    
    /* Do the command, length */
    packet.insert(packet.begin(), protocol::CMD_OUT_PARAMS_SET);
    packet.insert(packet.begin(), protocol::SET_PACKET_LEN + 2); /* Add 2 bytes as we added a command and will add checksum */

    /* Do checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 0 ; i < packet.size() ; i++)
    {
        checksum += packet[i];
    }
    packet.push_back(checksum);

    /* Do SYNC bytes */
    packet.insert(packet.begin(), protocol::SYNC);
    packet.insert(packet.begin(), protocol::SYNC);

    this->last_packet_sent_ = now;       /* Save the time when we sent the last packet */
    this->wait_response_ = true;
    write_array(packet);                 /* Sent the packet by UART */
    log_packet(packet, true);            /* Log uart for debug purposes */
    this->record_debug_packet_(packet, true);

    /* update setting state-machine */
    switch(this->update_)
    {
        case ACUpdate::NoUpdate:
            break;
        case ACUpdate::UpdateStart:
            this->update_ = ACUpdate::UpdateClear;
            break;
        case ACUpdate::UpdateClear:
            this->update_ = ACUpdate::NoUpdate;
            break;
        default:
            this->update_ = ACUpdate::NoUpdate;
            break;
    }
}

/*
 * Packet handling
 */

bool SinclairACCNT::verify_packet()
{
    /* At least 2 sync bytes + length + type + checksum */
    if (this->serialProcess_.data.size() < 5)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (length)");
        return false;
    }

    /* The header (aka sync bytes) was checked by SinclairAC::read_data() */

    /* The frame len was assumed by SinclairAC::read_data() */

    /* Check if this packet type sould be processed */
    bool commandAllowed = false;
    for (uint8_t packet : allowedPackets)
    {
        if (this->serialProcess_.data[3] == packet)
        {
            commandAllowed = true;
            break;
        }
    }
    if (!commandAllowed)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (command [%02X] not allowed)", this->serialProcess_.data[3]);
        return false;
    }

    /* Check checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 2 ; i < this->serialProcess_.data.size() - 1 ; i++)
    {
        checksum += this->serialProcess_.data[i];
    }
    if (checksum != this->serialProcess_.data[this->serialProcess_.data.size()-1])
    {
        ESP_LOGD(TAG, "Dropping invalid packet (checksum)");
        return false;
    }

    return true;
}

void SinclairACCNT::handle_packet()
{
    if (this->serialProcess_.data[3] == protocol::CMD_IN_UNIT_REPORT)
    {
        /* here we will remove unnecessary elements - header and checksum */
        this->serialProcess_.data.erase(this->serialProcess_.data.begin(), this->serialProcess_.data.begin() + 4); /* remove header */
        this->serialProcess_.data.pop_back();  /* remove checksum */
        /* now process the data */
        this->processUnitReport();
        this->publish_state();
    }
    else 
    {
        ESP_LOGD(TAG, "Received unknown packet");
    }
}

/*
 * This decodes frame recieved from AC Unit
 */
bool SinclairACCNT::processUnitReport()
{
    bool hasChanged = false;

    climate::ClimateMode newMode = determine_mode();
    if (this->mode != newMode) hasChanged = true;
    this->mode = newMode;

    const char* newFanMode = determine_fan_mode();
    if (this->has_custom_fan_mode())
    {
        if (strcmp(this->get_custom_fan_mode().c_str(), newFanMode) != 0) hasChanged = true;
    }
    else
    {
        hasChanged = true;
    }
    this->set_custom_fan_mode_(newFanMode);
    
    uint16_t newTargetTemperatureRaw;
    if (this->is_short_report_())
    {
        newTargetTemperatureRaw = this->serialProcess_.data[protocol::REPORT_SHORT_TEMP_SET_LO_BYTE]
            + ((this->serialProcess_.data[protocol::REPORT_SHORT_TEMP_SET_HI_BYTE] & protocol::REPORT_TEMP_SET_HI_MASK) << 8);
    }
    else
    {
        newTargetTemperatureRaw = this->serialProcess_.data[protocol::REPORT_TEMP_SET_LO_BYTE]
            + ((this->serialProcess_.data[protocol::REPORT_TEMP_SET_HI_BYTE] & protocol::REPORT_TEMP_SET_HI_MASK) << 8);
    }
    float newTargetTemperature = protocol::REPORT_TEMP_SET_C_BASE
        + static_cast<float>(newTargetTemperatureRaw - protocol::REPORT_TEMP_SET_RAW_BASE) / protocol::REPORT_TEMP_SET_RAW_STEP;
    this->target_temperature_reported_ = newTargetTemperature;
    if (this->target_temperature != newTargetTemperature) hasChanged = true;
    this->update_target_temperature(newTargetTemperature);
    
    /* if there is no external sensor mapped to represent current temperature we will get data from AC unit */
    if (this->current_temperature_sensor_ == nullptr)
    {
        float newCurrentTemperature;
        if (this->is_short_report_())
        {
            newCurrentTemperature = static_cast<float>(this->serialProcess_.data[protocol::REPORT_SHORT_TEMP_ACT_BYTE]
                + protocol::REPORT_SHORT_TEMP_ACT_OFF);
        }
        else
        {
            newCurrentTemperature = (float)(((this->serialProcess_.data[protocol::REPORT_TEMP_ACT_BYTE] & protocol::REPORT_TEMP_ACT_MASK) >> protocol::REPORT_TEMP_ACT_POS)
                - protocol::REPORT_TEMP_ACT_OFF) / protocol::REPORT_TEMP_ACT_DIV;
        }
        if (this->current_temperature != newCurrentTemperature) hasChanged = true;
        this->update_current_temperature(newCurrentTemperature);
    }

    std::string verticalSwing = determine_vertical_swing();
    std::string horizontalSwing = determine_horizontal_swing();

    this->update_swing_vertical(verticalSwing);
    this->update_swing_horizontal(horizontalSwing);

    climate::ClimateSwingMode newSwingMode;
    /* update legacy swing mode to somehow represent actual state and support
       this setting without detailed settings done with additional switches */
    if (verticalSwing == vertical_swing_options::FULL && horizontalSwing == horizontal_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_BOTH;
    else if (verticalSwing == vertical_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_VERTICAL;
    else if (horizontalSwing == horizontal_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_HORIZONTAL;
    else
        newSwingMode = climate::CLIMATE_SWING_OFF;
    
    if (this->swing_mode != newSwingMode) hasChanged = true;
    this->swing_mode = newSwingMode;

    this->update_display(determine_display());
    this->update_display_unit(determine_display_unit());

    this->update_plasma(determine_plasma());
    this->update_sleep(determine_sleep());
    this->update_xfan(determine_xfan());
    this->update_save(determine_save());

    return hasChanged;
}

bool SinclairACCNT::is_short_report_()
{
    return this->serialProcess_.data.size() == protocol::REPORT_SHORT_DATA_LEN;
}

bool SinclairACCNT::determine_power()
{
    if (this->is_short_report_())
    {
        return (this->serialProcess_.data[protocol::REPORT_SHORT_PWR_BYTE] & protocol::REPORT_SHORT_PWR_MASK) != 0;
    }

    return (this->serialProcess_.data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;
}

climate::ClimateMode SinclairACCNT::determine_mode()
{
    uint8_t mode;
    if (this->is_short_report_())
    {
        mode = this->serialProcess_.data[protocol::REPORT_SHORT_MODE_BYTE];
    }
    else
    {
        mode = (this->serialProcess_.data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS;
    }

    /* as mode presented by climate component incorporates both power and mode we will store this separately for Sinclair
       in _internal_ fields */
    /* check unit power flag */
    this->power_internal_ = this->determine_power();

    /* check unit mode */
    if (this->is_short_report_())
    {
        switch (mode)
        {
            case protocol::REPORT_SHORT_MODE_COOL:
                this->mode_internal_ = climate::CLIMATE_MODE_COOL;
                break;
            case protocol::REPORT_SHORT_MODE_DRY:
                this->mode_internal_ = climate::CLIMATE_MODE_DRY;
                break;
            case protocol::REPORT_SHORT_MODE_FAN:
                this->mode_internal_ = climate::CLIMATE_MODE_FAN_ONLY;
                break;
            case protocol::REPORT_SHORT_MODE_HEAT:
                this->mode_internal_ = climate::CLIMATE_MODE_HEAT;
                break;
            default:
                ESP_LOGW(TAG, "Received unknown short-report climate mode");
                if (this->mode_internal_ == climate::CLIMATE_MODE_OFF || this->mode_internal_ == climate::CLIMATE_MODE_HEAT_COOL)
                {
                    this->mode_internal_ = climate::CLIMATE_MODE_COOL;
                }
                break;
        }
    }
    else switch (mode)
    {
        case protocol::REPORT_MODE_COOL:
            this->mode_internal_ = climate::CLIMATE_MODE_COOL;
            break;
        case protocol::REPORT_MODE_DRY:
            this->mode_internal_ = climate::CLIMATE_MODE_DRY;
            break;
        case protocol::REPORT_MODE_FAN:
            this->mode_internal_ = climate::CLIMATE_MODE_FAN_ONLY;
            break;
        case protocol::REPORT_MODE_HEAT:
            this->mode_internal_ = climate::CLIMATE_MODE_HEAT;
            break;
        case protocol::REPORT_MODE_AUTO:
            /* This model does not expose AUTO in mode bits; 0 means no explicit mode change. */
            if (this->mode_internal_ == climate::CLIMATE_MODE_OFF || this->mode_internal_ == climate::CLIMATE_MODE_HEAT_COOL)
            {
                this->mode_internal_ = climate::CLIMATE_MODE_COOL;
            }
            break;
        default:
            ESP_LOGW(TAG, "Received unknown climate mode");
            if (this->mode_internal_ == climate::CLIMATE_MODE_OFF || this->mode_internal_ == climate::CLIMATE_MODE_HEAT_COOL)
            {
                this->mode_internal_ = climate::CLIMATE_MODE_COOL;
            }
            break;
    }

    /* if unit is powered on - return the mode, otherwise return CLIMATE_MODE_OFF */
    if (this->power_internal_)
    {
        return this->mode_internal_;
    }
    else
    {
        return climate::CLIMATE_MODE_OFF;
    }
}

const char* SinclairACCNT::determine_fan_mode()
{
    /* fan setting has quite complex representation in the packet, brace for it */
    uint8_t fanSpeed1;
    uint8_t fanSpeed2;
    bool fanQuiet;
    bool fanTurbo;
    if (this->is_short_report_())
    {
        fanSpeed1 = (this->serialProcess_.data[protocol::REPORT_SHORT_FAN_SPD1_BYTE] & protocol::REPORT_FAN_SPD1_MASK) >> protocol::REPORT_FAN_SPD1_POS;
        fanSpeed2 = 0;
        fanQuiet = false;
        fanTurbo = false;
    }
    else
    {
        fanSpeed1 = (this->serialProcess_.data[protocol::REPORT_FAN_SPD1_BYTE]  & protocol::REPORT_FAN_SPD1_MASK) >> protocol::REPORT_FAN_SPD1_POS;
        fanSpeed2 = (this->serialProcess_.data[protocol::REPORT_FAN_SPD2_BYTE]  & protocol::REPORT_FAN_SPD2_MASK) >> protocol::REPORT_FAN_SPD2_POS;
        fanQuiet  = (this->serialProcess_.data[protocol::REPORT_FAN_QUIET_BYTE] & protocol::REPORT_FAN_QUIET_MASK) != 0;
        fanTurbo  = (this->serialProcess_.data[protocol::REPORT_FAN_TURBO_BYTE] & protocol::REPORT_FAN_TURBO_MASK) != 0;
    }
    (void) fanSpeed2;
    (void) fanQuiet;
    (void) fanTurbo;
    /* we have extracted all the data, let's do the processing */
    if      (fanSpeed1 == 0)
    {
        return fan_modes::FAN_TURBO;
    }
    else if (fanSpeed1 == 1)
    {
        return fan_modes::FAN_AUTO;
    }
    else if (fanSpeed1 == 2)
    {
        return fan_modes::FAN_LOW;
    }
    else if (fanSpeed1 == 4)
    {
        return fan_modes::FAN_MED;
    }
    else if (fanSpeed1 == 6)
    {
        return fan_modes::FAN_HIGH;
    }
    else 
    {
        ESP_LOGW(TAG, "Received unknown fan mode");
        return fan_modes::FAN_AUTO;
    }
}

std::string SinclairACCNT::determine_vertical_swing()
{
    uint8_t mode;
    if (this->is_short_report_())
    {
        mode = (this->serialProcess_.data[protocol::REPORT_SHORT_VSWING_BYTE] & protocol::REPORT_VSWING_MASK) >> protocol::REPORT_VSWING_POS;
    }
    else
    {
        mode = (this->serialProcess_.data[protocol::REPORT_VSWING_BYTE] & protocol::REPORT_VSWING_MASK) >> protocol::REPORT_VSWING_POS;
    }

    switch (mode) {
        case protocol::REPORT_VSWING_OFF:
            return vertical_swing_options::OFF;
        case protocol::REPORT_VSWING_FULL:
            return vertical_swing_options::FULL;
        case protocol::REPORT_VSWING_DOWN:
            return vertical_swing_options::DOWN;
        case protocol::REPORT_VSWING_MIDD:
            return vertical_swing_options::MIDD;
        case protocol::REPORT_VSWING_MID:
            return vertical_swing_options::MID;
        case protocol::REPORT_VSWING_MIDU:
            return vertical_swing_options::MIDU;
        case protocol::REPORT_VSWING_UP:
            return vertical_swing_options::UP;
        case protocol::REPORT_VSWING_CDOWN:
            return vertical_swing_options::CDOWN;
        case protocol::REPORT_VSWING_CMIDD:
            return vertical_swing_options::CMIDD;
        case protocol::REPORT_VSWING_CMID:
            return vertical_swing_options::CMID;
        case protocol::REPORT_VSWING_CMIDU:
            return vertical_swing_options::CMIDU;
        case protocol::REPORT_VSWING_CUP:
            return vertical_swing_options::CUP;
        default:
            ESP_LOGW(TAG, "Received unknown vertical swing mode");
            return vertical_swing_options::OFF;;
    }
}

std::string SinclairACCNT::determine_horizontal_swing()
{
    uint8_t mode;
    if (this->is_short_report_())
    {
        mode = (this->serialProcess_.data[protocol::REPORT_SHORT_HSWING_BYTE] & protocol::REPORT_HSWING_MASK) >> protocol::REPORT_HSWING_POS;
    }
    else
    {
        mode = (this->serialProcess_.data[protocol::REPORT_HSWING_BYTE] & protocol::REPORT_HSWING_MASK) >> protocol::REPORT_HSWING_POS;
    }

    switch (mode) {
        case protocol::REPORT_HSWING_OFF:
            return horizontal_swing_options::OFF;
        case protocol::REPORT_HSWING_FULL:
            return horizontal_swing_options::FULL;
        case protocol::REPORT_HSWING_CLEFT:
            return horizontal_swing_options::CLEFT;
        case protocol::REPORT_HSWING_CMIDL:
            return horizontal_swing_options::CMIDL;
        case protocol::REPORT_HSWING_CMID:
            return horizontal_swing_options::CMID;
        case protocol::REPORT_HSWING_CMIDR:
            return horizontal_swing_options::CMIDR;
        case protocol::REPORT_HSWING_CRIGHT:
            return horizontal_swing_options::CRIGHT;
        default:
            ESP_LOGW(TAG, "Received unknown horizontal swing mode");
            return horizontal_swing_options::OFF;
    }
}

std::string SinclairACCNT::determine_display()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_DISP_MODE_BYTE] & protocol::REPORT_DISP_MODE_MASK) >> protocol::REPORT_DISP_MODE_POS;

    this->display_power_internal_ = (this->serialProcess_.data[protocol::REPORT_DISP_ON_BYTE] & protocol::REPORT_DISP_ON_MASK);

    switch (mode) {
        case protocol::REPORT_DISP_MODE_AUTO:
            this->display_mode_internal_ = display_options::AUTO;
            break;
        case protocol::REPORT_DISP_MODE_SET:
            this->display_mode_internal_ = display_options::SET;
            break;
        case protocol::REPORT_DISP_MODE_ACT:
            this->display_mode_internal_ = display_options::ACT;
            break;
        case protocol::REPORT_DISP_MODE_OUT:
            this->display_mode_internal_ = display_options::OUT;
            break;
        default:
            ESP_LOGW(TAG, "Received unknown display mode");
            this->display_mode_internal_ = display_options::AUTO;
            break;
    }

    if (this->display_power_internal_)
    {
        return this->display_mode_internal_;
    }
    else
    {
        return display_options::OFF;
    }
}

std::string SinclairACCNT::determine_display_unit()
{
    if (this->serialProcess_.data[protocol::REPORT_DISP_F_BYTE] & protocol::REPORT_DISP_F_MASK)
    {
        return display_unit_options::DEGF;
    }
    else
    {
        return display_unit_options::DEGC;
    }
}

bool SinclairACCNT::determine_plasma(){
    bool plasma1 = (this->serialProcess_.data[protocol::REPORT_PLASMA1_BYTE] & protocol::REPORT_PLASMA1_MASK) != 0;
    bool plasma2 = (this->serialProcess_.data[protocol::REPORT_PLASMA2_BYTE] & protocol::REPORT_PLASMA2_MASK) != 0;
    return plasma1 || plasma2;
}

bool SinclairACCNT::determine_sleep(){
    return (this->serialProcess_.data[protocol::REPORT_SLEEP_BYTE] & protocol::REPORT_SLEEP_MASK) != 0;
}

bool SinclairACCNT::determine_xfan(){
    return (this->serialProcess_.data[protocol::REPORT_XFAN_BYTE] & protocol::REPORT_XFAN_MASK) != 0;
}

bool SinclairACCNT::determine_save(){
    return (this->serialProcess_.data[protocol::REPORT_SAVE_BYTE] & protocol::REPORT_SAVE_MASK) != 0;
}


/*
 * Sensor handling
 */

void SinclairACCNT::on_vertical_swing_change(const std::string &swing)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting vertical swing position");

    this->update_ = ACUpdate::UpdateStart;
    this->vertical_swing_state_ = swing;
}

void SinclairACCNT::on_horizontal_swing_change(const std::string &swing)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting horizontal swing position");

    this->update_ = ACUpdate::UpdateStart;
    this->horizontal_swing_state_ = swing;
}

void SinclairACCNT::on_display_change(const std::string &display)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display mode");

    this->update_ = ACUpdate::UpdateStart;
    this->display_state_ = display;
}

void SinclairACCNT::on_display_unit_change(const std::string &display_unit)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display unit");

    this->update_ = ACUpdate::UpdateStart;
    this->display_unit_state_ = display_unit;
}

void SinclairACCNT::on_plasma_change(bool plasma)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting plasma");

    this->update_ = ACUpdate::UpdateStart;
    this->plasma_state_ = plasma;
}

void SinclairACCNT::on_sleep_change(bool sleep)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting sleep");

    this->update_ = ACUpdate::UpdateStart;
    this->sleep_state_ = sleep;
}

void SinclairACCNT::on_xfan_change(bool xfan)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting xfan");

    this->update_ = ACUpdate::UpdateStart;
    this->xfan_state_ = xfan;
}

void SinclairACCNT::on_save_change(bool save)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting save");

    this->update_ = ACUpdate::UpdateStart;
    this->save_state_ = save;
}

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
