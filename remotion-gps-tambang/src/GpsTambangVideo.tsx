import React, { useMemo } from 'react';
import {
  AbsoluteFill,
  interpolate,
  Sequence,
  useCurrentFrame,
  useVideoConfig,
  spring,
  random,
  staticFile,
  Img,
} from 'remotion';

// ============================================================
// CONSTANTS
// ============================================================
const W = 1080;
const H = 1920;

// Color palette — matches the plan
const C = {
  bg: '#030712',
  bgMid: '#0f172a',
  blue: '#38bdf8',   // WiFi Internet / realtime
  amber: '#f59e0b',  // EXCA relay data
  green: '#10b981',  // DT own data
  pink: '#ec4899',   // MQTT / VPS
  red: '#ef4444',    // No signal / problem
  gray: '#334155',
  textPrimary: '#f8fafc',
  textMuted: '#94a3b8',
};

// ============================================================
// SHARED BACKGROUND
// ============================================================
// Dark topo background (used for non-vehicle scenes)
const TopoBG: React.FC = () => {
  const frame = useCurrentFrame();
  const yOff = (frame * 0.4) % 80;
  const particles = useMemo(() =>
    Array.from({ length: 60 }).map((_, i) => ({
      x: random(`px-${i}`) * W,
      y: random(`py-${i}`) * H,
      spd: random(`ps-${i}`) * 1.5 + 0.5,
      sz: random(`sz-${i}`) * 5 + 1,
      hue: random(`ph-${i}`) > 0.5 ? C.amber : C.blue,
    })), []);
  return (
    <AbsoluteFill style={{ background: `radial-gradient(ellipse at 50% 30%, #0c1a2e 0%, ${C.bg} 70%)` }}>
      <svg width={W} height={H} style={{ position: 'absolute', opacity: 0.15 }}>
        {Array.from({ length: 12 }).map((_, i) => (
          <ellipse key={i} cx={W / 2 + Math.sin(i * 0.7) * 100} cy={H / 2 + yOff + i * 90 - 400}
            rx={300 + i * 60} ry={80 + i * 20} fill="none" stroke={C.amber} strokeWidth="1.5" />
        ))}
        {Array.from({ length: 8 }).map((_, i) => (
          <ellipse key={`b-${i}`} cx={W / 2 - 80} cy={H * 0.7 + yOff + i * 110 - 300}
            rx={200 + i * 50} ry={60 + i * 18} fill="none" stroke={C.green} strokeWidth="1" />
        ))}
      </svg>
      {particles.map((p, i) => (
        <div key={i} style={{
          position: 'absolute', left: p.x,
          top: ((p.y - frame * p.spd) % H + H) % H,
          width: p.sz, height: p.sz, borderRadius: '50%',
          backgroundColor: p.hue,
          opacity: 0.25 + Math.sin(frame / 15 + i) * 0.2,
          boxShadow: `0 0 ${p.sz * 3}px ${p.hue}`,
        }} />
      ))}
    </AbsoluteFill>
  );
};

// Mining terrain satellite-map background (blurred) — used for vehicle scenes S2-S5
const MapBG: React.FC = () => {
  const frame = useCurrentFrame();
  // Very slow pan to give sense of movement
  const panX = Math.sin(frame / 300) * 30;
  const panY = Math.cos(frame / 400) * 20;

  return (
    <AbsoluteFill style={{ overflow: 'hidden' }}>
      {/* The raw map SVG — will be blurred */}
      <div style={{
        position: 'absolute', width: '110%', height: '110%',
        top: `${-5 + panY}px`, left: `${-5 + panX}px`,
        filter: 'blur(8px) brightness(0.55) saturate(1.4)',
        transform: 'scale(1.05)',
      }}>
        <svg width={W * 1.1} height={H * 1.1} xmlns="http://www.w3.org/2000/svg">
          {/* Base ground — laterite orange-brown (mine area) */}
          <rect width="100%" height="100%" fill="#5c3a1e" />

          {/* Overburden / excavated area — pale brown */}
          <ellipse cx={540} cy={500} rx={380} ry={280} fill="#c87941" opacity="0.7" />
          <ellipse cx={300} cy={850} rx={260} ry={180} fill="#b06530" opacity="0.6" />
          <ellipse cx={780} cy={1100} rx={300} ry={200} fill="#c87941" opacity="0.5" />

          {/* Vegetation patches — dark green */}
          <ellipse cx={150} cy={300} rx={200} ry={150} fill="#1a4d1a" opacity="0.8" />
          <ellipse cx={900} cy={400} rx={160} ry={130} fill="#1f5c1f" opacity="0.75" />
          <ellipse cx={100} cy={1200} rx={220} ry={160} fill="#174517" opacity="0.85" />
          <ellipse cx={950} cy={1500} rx={180} ry={140} fill="#1a4d1a" opacity="0.7" />
          <ellipse cx={500} cy={1700} rx={250} ry={120} fill="#1f5c1f" opacity="0.6" />

          {/* Haul roads — packed gravel (light tan) */}
          {/* Main haul road sweeping diagonally */}
          <path d="M 0 1800 Q 200 1400 300 1000 Q 400 700 540 500 Q 650 350 900 200"
            stroke="#d4aa6e" strokeWidth="55" fill="none" strokeLinecap="round" />
          {/* Secondary road */}
          <path d="M 1080 1600 Q 800 1400 700 1100 Q 600 850 540 700"
            stroke="#c49a58" strokeWidth="40" fill="none" strokeLinecap="round" />
          {/* Connector road */}
          <path d="M 0 600 Q 150 700 300 850 Q 420 950 540 1000"
            stroke="#c49a58" strokeWidth="35" fill="none" strokeLinecap="round" />

          {/* Road center lines (dashed white) */}
          <path d="M 0 1800 Q 200 1400 300 1000 Q 400 700 540 500 Q 650 350 900 200"
            stroke="#ffffff" strokeWidth="5" fill="none" strokeDasharray="30 25" opacity="0.3" />

          {/* Mining pit / excavation face — very dark brown */}
          <ellipse cx={540} cy={550} rx={200} ry={140} fill="#2d1a0a" opacity="0.85" />
          <ellipse cx={540} cy={550} rx={140} ry={90} fill="#1a0d05" opacity="0.9" />

          {/* Settling pond / water — dark blue */}
          <ellipse cx={820} cy={1600} rx={150} ry={100} fill="#1e3a5f" opacity="0.9" />
          <ellipse cx={820} cy={1600} rx={100} ry={65} fill="#25497a" opacity="0.7" />

          {/* Topographic contour lines overlay */}
          {Array.from({ length: 10 }).map((_, i) => (
            <ellipse key={`topo-${i}`}
              cx={540 + Math.sin(i * 0.8) * 40}
              cy={550 + i * 140}
              rx={220 + i * 75}
              ry={80 + i * 35}
              fill="none" stroke="#ffffff" strokeWidth="1.5" opacity={0.08 + i * 0.005}
            />
          ))}

          {/* Rock dump / stockpile — grey mound */}
          <ellipse cx={200} cy={1500} rx={160} ry={100} fill="#6b7280" opacity="0.7" />
          <ellipse cx={200} cy={1480} rx={110} ry={65} fill="#9ca3af" opacity="0.5" />
        </svg>
      </div>

      {/* Dark gradient overlay to make it feel like a map view from above */}
      <div style={{
        position: 'absolute', width: '100%', height: '100%',
        background: 'linear-gradient(180deg, rgba(3,7,18,0.55) 0%, rgba(3,7,18,0.35) 50%, rgba(3,7,18,0.6) 100%)',
      }} />

      {/* Subtle vignette edges */}
      <div style={{
        position: 'absolute', width: '100%', height: '100%',
        background: 'radial-gradient(ellipse at 50% 50%, transparent 50%, rgba(3,7,18,0.7) 100%)',
      }} />

      {/* Faint grid (map tile lines) */}
      <div style={{
        position: 'absolute', width: '100%', height: '100%',
        backgroundImage: `linear-gradient(rgba(56,189,248,0.04) 1px, transparent 1px),
                          linear-gradient(90deg, rgba(56,189,248,0.04) 1px, transparent 1px)`,
        backgroundSize: '80px 80px',
      }} />
    </AbsoluteFill>
  );
};

// ============================================================
// REUSABLE ATOMS
// ============================================================
const Icon: React.FC<{ ch: string; size?: number; glow?: string; style?: React.CSSProperties }> = ({ ch, size = 180, glow, style }) => (
  <div style={{
    fontSize: size,
    lineHeight: 1,
    filter: glow ? `drop-shadow(0 0 30px ${glow})` : 'drop-shadow(0 10px 20px rgba(0,0,0,0.8))',
    ...style,
  }}>{ch}</div>
);

// Animated WiFi ripple centered on position
const WifiRipple: React.FC<{ color?: string }> = ({ color = C.blue }) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill>
      {[0, 15, 30].map(delay => {
        const p = ((frame - delay) % 45) / 45;
        if (p < 0 || p > 1) return null;
        return (
          <div key={delay} style={{
            position: 'absolute',
            left: '50%', top: '50%',
            transform: 'translate(-50%,-50%)',
            width: 40 + p * 400,
            height: 40 + p * 400,
            border: `6px solid ${color}`,
            borderRadius: '50%',
            opacity: (1 - p) * 0.8,
          }} />
        );
      })}
    </AbsoluteFill>
  );
};

// Stream of data packets traveling from (x1,y1) to (x2,y2)
interface StreamProps { x1: number; y1: number; x2: number; y2: number; color?: string; count?: number; speed?: number }
const DataStream: React.FC<StreamProps> = ({ x1, y1, x2, y2, color = C.blue, count = 6, speed = 30 }) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill>
      {Array.from({ length: count }).map((_, i) => {
        const p = ((frame + i * (speed / count)) % speed) / speed;
        const x = interpolate(p, [0, 1], [x1, x2]);
        const y = interpolate(p, [0, 1], [y1, y2]);
        return (
          <div key={i} style={{
            position: 'absolute',
            left: x - 14,
            top: y - 14,
            width: 28,
            height: 28,
            borderRadius: '50%',
            backgroundColor: color,
            boxShadow: `0 0 20px 4px ${color}`,
            opacity: p < 0.08 ? p / 0.08 : p > 0.92 ? (1 - p) / 0.08 : 1,
          }} />
        );
      })}
    </AbsoluteFill>
  );
};

// SD card fill indicator
const SDCard: React.FC<{ fill: number; color?: string }> = ({ fill, color = C.green }) => (
  <div style={{
    width: 100, height: 130,
    border: `4px solid ${color}`,
    borderRadius: 10,
    backgroundColor: '#1e293b',
    display: 'flex', flexDirection: 'column', justifyContent: 'flex-end',
    overflow: 'hidden',
    boxShadow: `0 0 20px ${color}`,
  }}>
    <div style={{ width: '100%', height: `${fill * 100}%`, backgroundColor: color, opacity: 0.8, transition: 'height 0.3s' }} />
  </div>
);

// GSM No-Signal indicator
const NoSignalTower: React.FC = () => {
  const frame = useCurrentFrame();
  const blink = Math.sin(frame / 6) > 0;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 10 }}>
      <Icon ch="📵" size={150} style={{ filter: 'drop-shadow(0 0 30px #ef4444)' }} />
      <div style={{
        width: 80, height: 80, borderRadius: '50%',
        backgroundColor: blink ? C.red : 'transparent',
        border: `4px solid ${C.red}`,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
        fontSize: 40, fontWeight: 'bold', color: C.red,
      }}>✕</div>
    </div>
  );
};

// Glowing connection line between two absolute coordinates
const ConnLine: React.FC<{ x1: number; y1: number; x2: number; y2: number; color?: string; dashed?: boolean }> = ({ x1, y1, x2, y2, color = C.blue, dashed = false }) => {
  const dx = x2 - x1, dy = y2 - y1;
  const len = Math.sqrt(dx * dx + dy * dy);
  const angle = Math.atan2(dy, dx) * (180 / Math.PI);
  return (
    <div style={{
      position: 'absolute',
      left: x1, top: y1,
      width: len, height: 4,
      backgroundColor: color,
      borderRadius: 2,
      transform: `rotate(${angle}deg)`,
      transformOrigin: '0 50%',
      boxShadow: `0 0 15px 2px ${color}`,
      opacity: 0.85,
      backgroundImage: dashed ? `repeating-linear-gradient(90deg, ${color} 0, ${color} 15px, transparent 15px, transparent 30px)` : 'none',
      backgroundColor: dashed ? 'transparent' : color,
    }} />
  );
};

// ============================================================
// SCENE COMPONENTS
// ============================================================

// Scene 1: Blank Spot Problem (F 0–300)
const Scene1: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const appear = spring({ frame, fps, config: { damping: 14 } });
  const glitch = Math.sin(frame / 3) * 2;

  return (
    <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center' }}>
      <div style={{ transform: `scale(${appear}) translateX(${glitch}px)`, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 40 }}>
        <NoSignalTower />
        {/* Crossed-out signal bars */}
        <div style={{ display: 'flex', gap: 10, alignItems: 'flex-end', opacity: interpolate(frame, [0, 20], [0, 1]) }}>
          {[40, 65, 90, 120].map((h, i) => (
            <div key={i} style={{
              width: 28, height: h,
              backgroundColor: C.gray,
              borderRadius: 4,
              opacity: 0.3,
            }} />
          ))}
        </div>
      </div>
    </AbsoluteFill>
  );
};

// Scene 2: GPS + ESP32 + SD Card on Both Vehicles (F300–660)
const Scene2: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const excaIn = spring({ frame, fps, from: -350, to: W * 0.15, config: { damping: 15 } });
  const dtIn = spring({ frame: frame - 20, fps, from: W + 300, to: W * 0.5, config: { damping: 15 } });
  const sdFill = interpolate(frame, [60, 300], [0, 0.7], { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' });

  return (
    <AbsoluteFill>
      {/* Satellite */}
      <Icon ch="🛰️" size={120} glow={C.blue} style={{
        position: 'absolute', left: W * 0.35,
        top: 200 + Math.sin(frame / 20) * 15,
      }} />

      {/* GPS signal lines to both vehicles */}
      {frame > 30 && <ConnLine x1={W * 0.38} y1={320} x2={excaIn + 90} y2={820} color={C.blue} />}
      {frame > 50 && <ConnLine x1={W * 0.38} y1={320} x2={dtIn + 90} y2={820} color={C.blue} />}

      {/* EXCA */}
      <Icon ch="🚜" size={220} style={{ position: 'absolute', left: excaIn, top: 700 }} />
      {/* EXCA SD card */}
      <div style={{ position: 'absolute', left: excaIn + 10, top: 940 }}>
        <SDCard fill={sdFill} color={C.amber} />
      </div>

      {/* DT */}
      <Icon ch="🚚" size={220} style={{ position: 'absolute', left: dtIn, top: 700, transform: 'scaleX(-1)' }} />
      {/* DT SD card */}
      <div style={{ position: 'absolute', left: dtIn + 10, top: 940 }}>
        <SDCard fill={sdFill} color={C.green} />
      </div>

      {/* ESP32 chips blinking */}
      {frame > 80 && (
        <>
          <div style={{ position: 'absolute', left: excaIn + 80, top: 860, width: 60, height: 40, backgroundColor: '#0f4c2a', border: `3px solid ${C.green}`, borderRadius: 8, boxShadow: Math.sin(frame / 5) > 0 ? `0 0 20px ${C.green}` : 'none', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 20, color: C.green, fontWeight: 'bold' }}>⚙</div>
          <div style={{ position: 'absolute', left: dtIn + 80, top: 860, width: 60, height: 40, backgroundColor: '#0f4c2a', border: `3px solid ${C.green}`, borderRadius: 8, boxShadow: Math.sin(frame / 5 + 2) > 0 ? `0 0 20px ${C.green}` : 'none', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 20, color: C.green, fontWeight: 'bold' }}>⚙</div>
        </>
      )}
    </AbsoluteFill>
  );
};

// Scene 3: EXCA Has WiFi → Direct Realtime MQTT (F660–960)
const Scene3: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const appear = spring({ frame, fps, config: { damping: 12 } });
  const showStream = frame > 60;
  const EXCA_X = 150, EXCA_Y = 900;
  const TOWER_X = 750, TOWER_Y = 900;
  const CLOUD_X = 540, CLOUD_Y = 250;

  return (
    <AbsoluteFill style={{ opacity: appear }}>
      {/* Tower WiFi */}
      <Icon ch="📡" size={200} glow={C.blue} style={{ position: 'absolute', left: TOWER_X - 100, top: TOWER_Y - 180 }} />
      {/* WiFi ripple from tower */}
      {showStream && (
        <div style={{ position: 'absolute', left: TOWER_X - 200, top: TOWER_Y - 200 }}>
          <WifiRipple color={C.blue} />
        </div>
      )}

      {/* EXCA */}
      <Icon ch="🚜" size={220} style={{ position: 'absolute', left: EXCA_X, top: EXCA_Y - 200 }} />
      <div style={{ position: 'absolute', left: EXCA_X + 20, top: EXCA_Y + 50 }}>
        <SDCard fill={0.4} color={C.amber} />
      </div>

      {/* Connection line: EXCA → Tower */}
      {showStream && <ConnLine x1={EXCA_X + 110} y1={EXCA_Y - 100} x2={TOWER_X - 100} y2={TOWER_Y - 80} color={C.blue} />}

      {/* Cloud / MQTT */}
      <Icon ch="☁️" size={200} glow={C.pink} style={{ position: 'absolute', left: CLOUD_X - 100, top: CLOUD_Y - 80 }} />
      <div style={{ position: 'absolute', left: CLOUD_X - 50, top: CLOUD_Y + 120, fontSize: 32, color: C.pink, fontWeight: 'bold', letterSpacing: 4 }}>MQTT</div>

      {/* Connection: Tower → Cloud */}
      {showStream && <ConnLine x1={TOWER_X - 60} y1={TOWER_Y - 180} x2={CLOUD_X} y2={CLOUD_Y + 100} color={C.blue} />}

      {/* Real-time data stream: EXCA → Cloud (fast, direct) */}
      {showStream && <DataStream x1={EXCA_X + 110} y1={EXCA_Y - 100} x2={CLOUD_X} y2={CLOUD_Y + 100} color={C.blue} count={10} speed={20} />}

      {/* Lightning bolt = real-time indicator */}
      {showStream && (
        <div style={{ position: 'absolute', left: CLOUD_X + 100, top: CLOUD_Y + 60, fontSize: 80, opacity: Math.sin(frame / 8) > 0 ? 1 : 0.3 }}>⚡</div>
      )}
    </AbsoluteFill>
  );
};

// Scene 4A: EXCA offline, SD filling (F960-1110)
const Scene4A: React.FC = () => {
  const frame = useCurrentFrame();
  const sdFill = interpolate(frame, [0, 120], [0.3, 0.9], { extrapolateRight: 'clamp' });
  const blink = Math.sin(frame / 4) > 0;
  return (
    <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center' }}>
      {/* No WiFi = X */}
      <div style={{ position: 'absolute', top: 300 }}>
        <Icon ch="📵" size={120} style={{ filter: `drop-shadow(0 0 20px ${C.red})` }} />
      </div>

      {/* EXCA alone */}
      <Icon ch="🚜" size={250} style={{ position: 'absolute', left: 380, top: 750 }} />
      {/* SD filling up */}
      <div style={{ position: 'absolute', left: 400, top: 1020 }}>
        <SDCard fill={sdFill} color={C.amber} />
      </div>

      {/* Dashed line to far-away cloud (can't reach) */}
      <ConnLine x1={540} y1={400} x2={900} y2={300} color={C.gray} dashed />
      <Icon ch="☁️" size={130} style={{ position: 'absolute', left: 820, top: 180, opacity: 0.25 }} />

      {/* Blinking warning */}
      <div style={{ position: 'absolute', left: 440, top: 680, fontSize: 60, opacity: blink ? 1 : 0.2 }}>⚠️</div>
    </AbsoluteFill>
  );
};

// Scene 4B: DT Arrives, scans EXCA WiFi, TCP transfer (F1110-1380)
const Scene4B: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const dtX = interpolate(frame, [0, 80], [-250, 100], { extrapolateRight: 'clamp' });
  const showHandshake = frame > 90;
  const showTransfer = frame > 120;
  const EXCA_X = 650, EXCA_Y = 800;
  const DT_X_FIXED = 100;

  // EXCA SD decreases as data is transferred out
  const excaFill = showTransfer
    ? interpolate(frame, [120, 270], [0.88, 0.05], { extrapolateRight: 'clamp' })
    : 0.88;

  // DT SD increases as it receives EXCA data (on top of its own green data)
  const dtFill = showTransfer
    ? interpolate(frame, [120, 270], [0.3, 0.9], { extrapolateRight: 'clamp' })
    : 0.3;

  // Amber portion within DT card = EXCA data received so far
  const dtAmberPortion = showTransfer
    ? interpolate(frame, [120, 270], [0, 0.6], { extrapolateRight: 'clamp' })
    : 0;

  return (
    <AbsoluteFill>
      {/* EXCA fixed right side */}
      <Icon ch="🚜" size={220} style={{ position: 'absolute', left: EXCA_X, top: EXCA_Y - 180 }} />
      {/* EXCA SD card — shrinks during transfer */}
      <div style={{ position: 'absolute', left: EXCA_X + 20, top: EXCA_Y + 60 }}>
        <SDCard fill={excaFill} color={C.amber} />
      </div>
      {/* EXCA WiFi AP ripple */}
      <div style={{ position: 'absolute', left: EXCA_X - 60, top: EXCA_Y - 200 }}>
        <WifiRipple color={C.amber} />
      </div>

      {/* DT moving in from left */}
      <Icon ch="🚚" size={220} style={{ position: 'absolute', left: dtX, top: EXCA_Y - 180, transform: 'scaleX(-1)' }} />
      {/* DT SD card — bicolor: green (own) at bottom, amber (EXCA) stacking on top */}
      <div style={{ position: 'absolute', left: dtX + 20, top: EXCA_Y + 60 }}>
        {/* Stacked visual: green base + amber overlay */}
        <div style={{ position: 'relative', width: 100, height: 130 }}>
          {/* Green own data */}
          <SDCard fill={dtFill} color={C.green} />
          {/* Amber EXCA portion overlaid on top (opacity) */}
          {dtAmberPortion > 0 && (
            <div style={{
              position: 'absolute', bottom: 0, left: 0, width: '100%',
              height: `${dtAmberPortion * 100}%`,
              background: `linear-gradient(to top, ${C.amber}cc, ${C.amber}55)`,
              borderRadius: '0 0 6px 6px',
              border: `2px solid ${C.amber}`,
              boxShadow: `0 0 10px ${C.amber}`,
            }} />
          )}
        </div>
      </div>

      {/* Handshake WiFi connect */}
      {showHandshake && (
        <ConnLine x1={DT_X_FIXED + 110} y1={EXCA_Y - 80} x2={EXCA_X} y2={EXCA_Y - 80} color={C.amber} />
      )}

      {/* TCP data packets flying EXCA → DT (amber = EXCA data) */}
      {showTransfer && (
        <DataStream
          x1={EXCA_X} y1={EXCA_Y - 60}
          x2={DT_X_FIXED + 120} y2={EXCA_Y - 60}
          color={C.amber} count={12} speed={18}
        />
      )}
    </AbsoluteFill>
  );
};

// Scene 4C: DT has dual data (F1380-1500)
const Scene4C: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const appear = spring({ frame, fps, config: { damping: 14 } });

  return (
    <AbsoluteFill style={{ alignItems: 'center', justifyContent: 'center', opacity: appear }}>
      <Icon ch="🚚" size={260} style={{ transform: 'scaleX(-1)', position: 'absolute', top: 700 }} />
      {/* Dual SD cards shown */}
      <div style={{ position: 'absolute', top: 960, left: 330, display: 'flex', gap: 30 }}>
        <SDCard fill={0.85} color={C.green} />
        <SDCard fill={0.85} color={C.amber} />
      </div>
      {/* Dual color glow under DT */}
      <div style={{ position: 'absolute', top: 1060, left: 200, width: 680, height: 8, background: `linear-gradient(to right, ${C.green}, ${C.amber})`, borderRadius: 4, boxShadow: `0 0 30px ${C.amber}` }} />
    </AbsoluteFill>
  );
};

// Scene 5: DT to Tower → Upload both streams (F1500-1710)
const Scene5: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const dtX = interpolate(frame, [0, 100], [0, 580], { extrapolateRight: 'clamp' });
  const showUpload = frame > 110;
  const TOWER_X = 740, TOWER_Y = 820;
  const CLOUD_X = 540, CLOUD_Y = 200;

  return (
    <AbsoluteFill>
      {/* Tower */}
      <Icon ch="📡" size={200} glow={C.blue} style={{ position: 'absolute', left: TOWER_X - 100, top: TOWER_Y - 160 }} />
      {showUpload && (
        <div style={{ position: 'absolute', left: TOWER_X - 200, top: TOWER_Y - 200 }}>
          <WifiRipple color={C.blue} />
        </div>
      )}

      {/* Cloud MQTT */}
      <Icon ch="☁️" size={200} glow={C.pink} style={{ position: 'absolute', left: CLOUD_X - 100, top: CLOUD_Y - 80 }} />
      <div style={{ position: 'absolute', left: CLOUD_X - 60, top: CLOUD_Y + 110, fontSize: 32, color: C.pink, letterSpacing: 4, fontWeight: 'bold' }}>MQTT</div>

      {/* Tower → Cloud */}
      {showUpload && <ConnLine x1={TOWER_X - 60} y1={TOWER_Y - 160} x2={CLOUD_X + 20} y2={CLOUD_Y + 100} color={C.blue} />}

      {/* DT moving to tower */}
      <Icon ch="🚚" size={220} style={{ position: 'absolute', left: dtX, top: TOWER_Y - 160, transform: 'scaleX(-1)' }} />

      {/* Two separate upload streams once DT reaches tower */}
      {showUpload && (
        <>
          {/* Green stream = DT own data */}
          <DataStream x1={dtX + 110} y1={TOWER_Y - 80} x2={CLOUD_X + 20} y2={CLOUD_Y + 100} color={C.green} count={8} speed={22} />
          {/* Amber stream = relayed EXCA data */}
          <DataStream x1={dtX + 110} y1={TOWER_Y - 50} x2={CLOUD_X + 60} y2={CLOUD_Y + 130} color={C.amber} count={8} speed={22} />
        </>
      )}
    </AbsoluteFill>
  );
};

// Glowing map marker
const MapMarker: React.FC<{ x: number; y: number; color: string; icon: string; label: string; pulse?: boolean }> = ({ x, y, color, icon, label, pulse }) => {
  const frame = useCurrentFrame();
  const scale = pulse ? 1 + Math.sin(frame / 8) * 0.08 : 1;
  return (
    <div style={{ position: 'absolute', left: x - 36, top: y - 80 }}>
      {/* Pulsing range circle */}
      {pulse && (
        <div style={{
          position: 'absolute', left: '50%', top: '100%',
          transform: 'translate(-50%,-50%)',
          width: 80 + Math.sin(frame / 10) * 30,
          height: 80 + Math.sin(frame / 10) * 30,
          borderRadius: '50%', border: `3px solid ${color}`,
          opacity: 0.3 + Math.sin(frame / 10) * 0.2,
        }} />
      )}
      {/* Pin */}
      <div style={{
        width: 72, height: 72, backgroundColor: color,
        borderRadius: '50% 50% 50% 0', transform: `rotate(-45deg) scale(${scale})`,
        boxShadow: `0 0 25px ${color}, 0 4px 15px rgba(0,0,0,0.5)`,
        border: '3px solid rgba(255,255,255,0.4)',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}>
        <div style={{ transform: 'rotate(45deg)', fontSize: 32 }}>{icon}</div>
      </div>
      {/* Label bubble */}
      <div style={{
        position: 'absolute', left: 80, top: 5,
        backgroundColor: 'rgba(0,0,0,0.8)', color, borderRadius: 20,
        padding: '4px 14px', fontSize: 22, fontWeight: 'bold',
        border: `2px solid ${color}`, whiteSpace: 'nowrap',
        boxShadow: `0 0 10px ${color}44`,
      }}>{label}</div>
    </div>
  );
};

// Scene 6: Live Dashboard with real OSM map (F1710-1800)
const Scene6: React.FC = () => {
  const frame = useCurrentFrame();
  const appear = interpolate(frame, [0, 30], [0, 1], { extrapolateRight: 'clamp' });
  const liveBlinkOn = Math.floor(frame / 15) % 2 === 0;

  // Simulated GPS movement on the real map — pixel coords tuned to OSM screenshot
  // EXCA: excavator digging near a pit area, moves in small radius
  const exca1X = 280 + Math.sin(frame / 22) * 55 + Math.sin(frame / 7) * 10;
  const exca1Y = 520 + Math.cos(frame / 28) * 35;
  const exca2X = 480 + Math.cos(frame / 18) * 40;
  const exca2Y = 820 + Math.sin(frame / 24) * 30;

  // DT: dump truck moving along haul road
  const dtProgress = (frame % 90) / 90;
  const dt1X = interpolate(dtProgress, [0, 0.5, 1], [150, 540, 820]);
  const dt1Y = interpolate(dtProgress, [0, 0.5, 1], [1100, 750, 500]);
  const dt2X = interpolate(dtProgress, [0, 0.5, 1], [820, 400, 150]);
  const dt2Y = interpolate(dtProgress, [0, 0.5, 1], [500, 900, 1100]);

  return (
    <AbsoluteFill style={{ opacity: appear }}>
      {/* Dashboard wrapper — edge-to-edge portrait */}
      <div style={{ width: W, height: H, position: 'relative', overflow: 'hidden' }}>

        {/* === REAL OPENSTREETMAP SCREENSHOT === */}
        <Img
          src={staticFile('osm_map.png')}
          style={{
            position: 'absolute', top: 0, left: 0,
            width: '100%', height: '100%',
            objectFit: 'cover', objectPosition: 'center',
          }}
        />

        {/* Slight dark overlay so UI elements are readable */}
        <div style={{
          position: 'absolute', width: '100%', height: '100%',
          background: 'rgba(0,0,0,0.25)',
        }} />

        {/* === VEHICLE MARKERS (animated) === */}
        <MapMarker x={exca1X} y={exca1Y} color={C.amber} icon="🚜" label="EXCA-01" pulse />
        <MapMarker x={exca2X} y={exca2Y} color={C.amber} icon="🚜" label="EXCA-02" />
        <MapMarker x={dt1X} y={dt1Y} color={C.green} icon="🚚" label="DT-01" pulse />
        <MapMarker x={dt2X} y={dt2Y} color={C.green} icon="🚚" label="DT-03" />

        {/* === DASHBOARD HUD OVERLAY (top) === */}
        <div style={{
          position: 'absolute', top: 0, left: 0, right: 0,
          background: 'linear-gradient(180deg, rgba(3,7,18,0.92) 0%, rgba(3,7,18,0) 100%)',
          padding: '50px 50px 80px',
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 20 }}>
            {/* Live indicator */}
            <div style={{
              width: 22, height: 22, borderRadius: '50%',
              backgroundColor: liveBlinkOn ? C.red : 'transparent',
              border: `3px solid ${C.red}`,
              boxShadow: liveBlinkOn ? `0 0 15px ${C.red}` : 'none',
            }} />
            <div style={{ fontSize: 42, fontWeight: '800', color: '#f8fafc', letterSpacing: 3 }}>KUTAI FLEET LIVE</div>
          </div>
          {/* Mini stats row */}
          <div style={{ display: 'flex', gap: 24, marginTop: 24 }}>
            {[
              { label: 'EXCA', count: '2', color: C.amber },
              { label: 'DT', count: '2', color: C.green },
              { label: 'ONLINE', count: '4', color: C.blue },
            ].map(s => (
              <div key={s.label} style={{
                background: `${s.color}22`,
                border: `2px solid ${s.color}`,
                borderRadius: 16, padding: '10px 24px',
                display: 'flex', flexDirection: 'column', alignItems: 'center',
              }}>
                <div style={{ fontSize: 44, fontWeight: 'bold', color: s.color }}>{s.count}</div>
                <div style={{ fontSize: 22, color: '#94a3b8' }}>{s.label}</div>
              </div>
            ))}
          </div>
        </div>

        {/* === BOTTOM INFO CARD (MQTT live feed) === */}
        <div style={{
          position: 'absolute', bottom: 0, left: 0, right: 0,
          background: 'linear-gradient(0deg, rgba(3,7,18,0.95) 60%, rgba(3,7,18,0) 100%)',
          padding: '80px 50px 60px',
        }}>
          {/* Latest payload indicator */}
          <div style={{ fontSize: 24, color: C.textMuted, marginBottom: 12 }}>kutai/fleet/data</div>
          <div style={{
            fontFamily: "'Courier New', monospace",
            fontSize: 26, color: C.green,
            background: 'rgba(16,185,129,0.1)',
            border: `1px solid ${C.green}44`,
            borderRadius: 12, padding: '20px 24px',
            opacity: liveBlinkOn ? 1 : 0.7,
          }}>
            {`{"src":"DT-01","lat":-0.52,"lon":117.10}`}
          </div>
          {/* OSM Attribution */}
          <div style={{ marginTop: 16, fontSize: 20, color: '#64748b' }}>© OpenStreetMap contributors</div>
        </div>
      </div>
    </AbsoluteFill>
  );
};

// ============================================================
// MAIN COMPOSITION
// ============================================================
export const GpsTambangVideo: React.FC = () => {
  // Scene timings (frames @ 30fps)
  // S1: 0-300    (00:00-00:10)
  // S2: 300-660  (00:10-00:22)
  // S3: 660-960  (00:22-00:32)
  // S4A: 960-1110 (00:32-00:37)
  // S4B: 1110-1380 (00:37-00:46)
  // S4C: 1380-1500 (00:46-00:50)
  // S5: 1500-1710 (00:50-00:57)
  // S6: 1710-1800 (00:57-01:00)

  return (
    <AbsoluteFill style={{ backgroundColor: C.bg, fontFamily: "'Inter', 'Helvetica', sans-serif" }}>

      {/* Scene 1: Dark topo for problem intro */}
      <Sequence from={0} durationInFrames={300}>
        <TopoBG />
        <Scene1 />
      </Sequence>

      {/* Scenes 2–5: Blurred map terrain background */}
      <Sequence from={300} durationInFrames={1410}>
        <MapBG />
      </Sequence>

      {/* Vehicle & data scenes */}
      <Sequence from={300} durationInFrames={360}><Scene2 /></Sequence>
      <Sequence from={660} durationInFrames={300}><Scene3 /></Sequence>
      <Sequence from={960} durationInFrames={150}><Scene4A /></Sequence>
      <Sequence from={1110} durationInFrames={270}><Scene4B /></Sequence>
      <Sequence from={1380} durationInFrames={120}><Scene4C /></Sequence>
      <Sequence from={1500} durationInFrames={210}><Scene5 /></Sequence>

      {/* Scene 6: Dashboard has its own background */}
      <Sequence from={1710} durationInFrames={90}><Scene6 /></Sequence>
    </AbsoluteFill>
  );
};
