import { chromium } from 'playwright'

const url = process.argv[2] || 'http://localhost:4321/'
const browser = await chromium.launch()
const page = await browser.newPage({ viewport: { width: 1440, height: 1200 } })
await page.goto(url, { waitUntil: 'load' })
await page.waitForTimeout(6500) // let boot finish
// reveal everything so off-screen sections aren't opacity:0
await page.evaluate(() => document.querySelectorAll('.reveal').forEach((r) => r.classList.add('is-in')))
await page.waitForTimeout(400)

const ids = ['top', 'bet', 'loop', 'contract', 'sandbox', 'world', 'boundary', 'arch', 'road']
for (const id of ids) {
  try {
    await page.locator('#' + id).screenshot({ path: `/tmp/a_${id}.png` })
  } catch (e) {
    console.log('FAIL', id, e.message.split('\n')[0])
  }
}
console.log('done')
await browser.close()
