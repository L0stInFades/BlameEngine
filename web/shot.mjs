import { chromium } from 'playwright'

const url = process.argv[2] || 'http://localhost:4321/'
const pfx = process.argv[3] || 'shot'

const browser = await chromium.launch()
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } })

const errors = []
page.on('pageerror', (e) => errors.push('PAGEERROR: ' + e.message))
page.on('console', (m) => {
  if (m.type() === 'error') errors.push('CONSOLE.error: ' + m.text())
})

await page.goto(url, { waitUntil: 'load' })

// 1) during boot
await page.waitForTimeout(2500)
await page.screenshot({ path: `/tmp/${pfx}1_boot.png` })

// 2) after boot — hero + three.js point cloud
await page.waitForTimeout(7000)
const bootGone = await page.evaluate(() => {
  const b = document.querySelector('.boot-screen')
  return b ? getComputedStyle(b).opacity : 'no-boot-el'
})
await page.screenshot({ path: `/tmp/${pfx}2_hero.png` })

// 3) scroll into the datasheet cards
await page.mouse.wheel(0, 1700)
await page.waitForTimeout(1800)
await page.screenshot({ path: `/tmp/${pfx}3_mid.png` })

// 4) whole page
await page.screenshot({ path: `/tmp/${pfx}4_full.png`, fullPage: true })

// 5) console interaction — click a command button, confirm log streams
const before = await page.evaluate(() => document.getElementById('cliLines')?.children.length ?? -1)
await page.click('#cliTerminal .term__btn')
await page.waitForTimeout(2600)
const after = await page.evaluate(() => ({
  n: document.getElementById('cliLines')?.children.length ?? -1,
  first: document.getElementById('cliLines')?.textContent?.slice(0, 50) ?? '',
}))
await page.screenshot({ path: `/tmp/${pfx}5_console.png` })

console.log('BOOT_OPACITY_AFTER_9S:', bootGone)
console.log('CONSOLE lines before/after click:', before, '->', JSON.stringify(after))
console.log('ERRORS:', errors.length ? '\n' + errors.join('\n') : 'none')
await browser.close()
