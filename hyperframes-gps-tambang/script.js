// Scene Sequence Logic (HyperFrames Time Manager)
const scenes = document.querySelectorAll('.scene');

function switchScene(index) {
    scenes.forEach(s => { 
        s.classList.remove('active'); 
        s.style.display = 'none'; 
    });
    
    const target = document.getElementById(`scene-${index}`);
    if(target) {
        target.style.display = 'block';
        // Trigger reflow for animations
        void target.offsetWidth;
        target.classList.add('active');
        
        // Custom logic per scene
        if(index === 3) {
            setTimeout(() => {
                document.querySelector('.wifi-ripple').classList.remove('hidden');
                generateParticles('.data-particles', 10, 'data-particle', 100);
            }, 3000); // Wait DT to arrive
        }
        if(index === 4) {
            setTimeout(() => {
                generateParticles('.upload-stream', 15, 'upload-particle', 150);
            }, 3000); // Wait DT to arrive at tower
        }
    }
}

function generateParticles(containerSelector, count, className, delayMs) {
    const container = document.querySelector(containerSelector);
    container.classList.remove('hidden');
    container.innerHTML = '';
    for(let i=0; i<count; i++) {
        const p = document.createElement('div');
        p.className = className;
        p.style.animationDelay = `${i * delayMs}ms`;
        // Randomize slight Y offset for effect
        p.style.marginTop = `${(Math.random() - 0.5) * 150}px`;
        container.appendChild(p);
    }
}

// Timeline
// Scene 1: 00:00 - 00:08 (8s)
// Scene 2: 00:08 - 00:20 (12s)
// Scene 3: 00:20 - 00:35 (15s)
// Scene 4: 00:35 - 00:48 (13s)
// Scene 5: 00:48 - 01:00 (12s)

const timeline = [
    { time: 0, scene: 1 },    
    { time: 8000, scene: 2 }, 
    { time: 20000, scene: 3 },
    { time: 35000, scene: 4 },
    { time: 48000, scene: 5 } 
];

// Start execution
timeline.forEach(event => {
    setTimeout(() => {
        switchScene(event.scene);
    }, event.time);
});
switchScene(1); // init
