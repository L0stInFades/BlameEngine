import { chromium } from 'playwright'

const url = process.argv[2] || 'http://localhost:4321/'
const label = process.argv[3] || 'x'

const browser = await chromium.launch()
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } })
const client = await page.context().newCDPSession(page)
await client.send('Performance.enable')

let layerCount = 0
await client.send('LayerTree.enable')
client.on('LayerTree.layerTreeDidChange', (e) => { if (e.layers) layerCount = e.layers.length })

await page.goto(url, { waitUntil: 'load' })
await page.waitForTimeout(7000) // let boot finish

const metrics = async () => Object.fromEntries((await client.send('Performance.getMetrics')).metrics.map((x) => [x.name, x.value]))

// optional warm-up: scroll through every section once (content-visibility:auto remembers real sizes),
// then return to top — this measures the steady state a returning/up-down scroller actually feels
if (process.argv[4] === 'warm') {
  for (let i = 0; i < 55; i++) { await page.mouse.wheel(0, 220); await page.waitForTimeout(20) }
  await page.waitForTimeout(400)
  for (let i = 0; i < 72; i++) { await page.mouse.wheel(0, -220); await page.waitForTimeout(16) }
  await page.waitForTimeout(700)
}

const m0 = await metrics()

// frame meter + scripted scroll
await page.evaluate(() => {
  window.__f = []
  let last = performance.now()
  let stop = false
  const tick = (t) => { window.__f.push(t - last); last = t; if (!stop) requestAnimationFrame(tick) }
  requestAnimationFrame(tick)
  window.__stop = () => { stop = true }
})
for (let i = 0; i < 45; i++) { await page.mouse.wheel(0, 150); await page.waitForTimeout(45) }
await page.waitForTimeout(400)
const frames = await page.evaluate(() => { window.__stop(); return window.__f })

const m1 = await metrics()
const fs = frames.slice(4).filter((x) => x > 0 && x < 250).sort((a, b) => a - b)
const sum = fs.reduce((a, b) => a + b, 0)
const out = {
  label,
  compositedLayers: layerCount,
  scrollFrames: fs.length,
  avg_ms: +(sum / fs.length).toFixed(2),
  p95_ms: +(fs[Math.floor(fs.length * 0.95)] || 0).toFixed(2),
  max_ms: +(fs[fs.length - 1] || 0).toFixed(2),
  pct_miss_120fps: +(100 * fs.filter((x) => x > 8.34).length / fs.length).toFixed(1),
  pct_miss_60fps: +(100 * fs.filter((x) => x > 16.7).length / fs.length).toFixed(1),
  // CDP main-thread WORK during the scroll (hardware-independent):
  scroll_LayoutDuration_ms: +(((m1.LayoutDuration || 0) - (m0.LayoutDuration || 0)) * 1000).toFixed(1),
  scroll_RecalcStyle_ms: +(((m1.RecalcStyleDuration || 0) - (m0.RecalcStyleDuration || 0)) * 1000).toFixed(1),
  scroll_Script_ms: +(((m1.ScriptDuration || 0) - (m0.ScriptDuration || 0)) * 1000).toFixed(1),
  scroll_Task_ms: +(((m1.TaskDuration || 0) - (m0.TaskDuration || 0)) * 1000).toFixed(1),
  scroll_LayoutCount: (m1.LayoutCount || 0) - (m0.LayoutCount || 0),
  scroll_RecalcStyleCount: (m1.RecalcStyleCount || 0) - (m0.RecalcStyleCount || 0),
}
console.log(JSON.stringify(out))
await browser.close()
