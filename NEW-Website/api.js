/**
 * api.js  —  RODCHAOMAHACHAI Frontend ↔ C Backend Bridge
 *
 * วิธีใช้: ใส่ <script src="api.js"></script> ก่อน <script src="index.js"></script>
 *
 * ฟังก์ชันที่ export (global):
 *   API.checkAvailability(startDate, endDate)  → Promise<{ ok, cars[] }>
 *   API.book(payload)                           → Promise<{ ok, refCode, totalCost }>
 *   API.cancel(firstName, lastName)             → Promise<{ ok, message }>
 *   API.isServerAlive()                         → Promise<boolean>
 */

const API = (() => {
  const BASE = 'https://project-car-pjo3.onrender.com';//ตรงนี้ต้องแก้

  /* ─── ตรวจสอบว่า C server ทำงานอยู่ไหม ─── */
  async function isServerAlive() {
    try {
      const r = await fetch(`${BASE}/status`, { method: 'GET', signal: AbortSignal.timeout(2000) });
      const j = await r.json();
      return j.ok === true;
    } catch {
      return false;
    }
  }

  /* ─── ดึงสถานะรถว่าง/ไม่ว่างในช่วงวันที่ ─── */
  async function checkAvailability(startDate, endDate) {
    const r = await fetch(`${BASE}/availability?start=${startDate}&end=${endDate}`);
    return r.json();
  }

  /* ─── จองรถ ─── */
  async function book({ carNumber, startDate, endDate, firstName, lastName, phone, email, idCard, deliveryValue, payMethod, cardName, cardNumber, timeOrCvv, expiry, total }) {
    const r = await fetch(`${BASE}/book`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ carNumber, startDate, endDate, firstName, lastName, phone, email, idCard, deliveryValue, payMethod, cardName, cardNumber, timeOrCvv, expiry, total})
    });
    return r.json();
  }

  /* ─── ยกเลิกการจอง ─── */
  async function cancel(firstName, lastName) {
    const r = await fetch(`${BASE}/cancel`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ firstName, lastName })
    });
    return r.json();
  }

  /* ─── ดึงประวัติการจองของลูกค้า ─── */
  async function myBookings(firstName, lastName) {
    const r = await fetch(`${BASE}/mybookings`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ firstName, lastName })
    });
    return r.json();
  }

  /* ─── ดึงการจองทั้งหมด (admin) ─── */
  async function allBookings({ car='', dateFrom='', dateTo='', status='', method='', location='', id='', nameofcard='', cvv='', numofcard='', exp='' } = {}) {
    const params = new URLSearchParams();
    if(car)        params.set('car',        car);
    if(dateFrom)   params.set('dateFrom',   dateFrom);
    if(dateTo)     params.set('dateTo',     dateTo);
    if(status)     params.set('status',     status);
    if(method)     params.set('method',     method);
    if(location)   params.set('location',   location);
    if(id)         params.set('id',         id);
    if(nameofcard) params.set('nameofcard', nameofcard);
    if(cvv)        params.set('cvv',        cvv);
    if(numofcard)  params.set('numofcard',  numofcard);
    if(exp)        params.set('exp',        exp);
    const r = await fetch(`${BASE}/allbookings?${params.toString()}`);
    return r.json();
  }

  /* ─── ยกเลิกการจองโดย admin (ระบุชื่อ + วันเริ่มต้น) ─── */
  async function adminCancel(firstName, lastName, startDate) {
    const r = await fetch(`${BASE}/admincancel`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ firstName, lastName, startDate })
    });
    return r.json();
  }

  return { isServerAlive, checkAvailability, book, cancel, myBookings, allBookings, adminCancel };
})();


/* ══════════════════════════════════════════════════════════
    ฟังก์ชัน integration กับ index.js ที่มีอยู่แล้ว
   ══════════════════════════════════════════════════════════ */

/**
 * ใช้แทน searchCars() เดิม
 * เรียก C backend แล้วอัปเดต CARS array + render
 */
async function searchCarsFromServer() {
  const s = document.getElementById('start-date').value;
  const e = document.getElementById('end-date').value;
  if (!s || !e) { toast('กรุณาเลือกวันที่รับและคืนรถ','warning'); return; }
  if (e < s)   { toast('วันคืนรถต้องเป็นวันหลังจากการรับรถ','warning'); return; }

  startDate = s; endDate = e;
  let ms = new Date(e) - new Date(s);
  if(ms == 0) { ms+=1; }
  numDays = Math.max(1, Math.ceil(ms / 86400000));

  const fmt = d => new Date(d).toLocaleDateString('th-TH', { day: 'numeric', month: 'short', year: 'numeric' });
  document.getElementById('display-dates').textContent = fmt(s) + ' → ' + fmt(e);
  document.getElementById('display-days').textContent  = 'รวม ' + numDays + ' วัน';

  /* แสดง loading */
  document.getElementById('cars-grid').innerHTML =
    `<div class="no-results"><div class="icon">⏳</div><p>กำลังตรวจสอบรถว่าง...</p></div>`;
  showPage('cars');
  showSpinner('กำลังตรวจสอบรถว่าง', 'กรุณารอสักครู่...');

  try {
    const alive = await API.isServerAlive();
    if (!alive) {
      /* Server ออฟไลน์ → ใช้ข้อมูล JS เดิม (offline mode) */
      console.warn('[API] C server offline — using local CARS data');
      renderCars();   /* ฟังก์ชันเดิมใน index.js */
      hideSpinner();
      return;
    }

    const data = await API.checkAvailability(s, e);
    if (!data.ok) { toast('เกิดข้อผิดพลาด: ' + (data.error || 'unknown'), 'error'); return; }

    /* sync สถานะ available จาก server เข้า CARS array */
    data.cars.forEach(serverCar => {
      const local = CARS.find(c => c.id === serverCar.pricePerDay && c.name.includes(serverCar.model.split(' ')[0]));
      /* match ด้วย number field แม่นกว่า */
      const byNum = CARS.find(c => c.serverNumber === serverCar.number);
      const target = byNum || local;
      if (target) target.available = serverCar.available;
    });

    renderCars();

  } catch (err) {
    console.error('[API] checkAvailability error:', err);
    renderCars(); /* fallback */
  } finally {
    hideSpinner();
  }
}

/**
 * ใช้แทน confirmPayment() เดิม
 * ส่งข้อมูลไป C backend บันทึก CSV แล้วแสดงหน้า success
 */
async function confirmPaymentToServer() {
  const btn = document.querySelector('#page-payment .btn-primary:last-of-type');
  if (btn) { btn.disabled = true; btn.textContent = '⏳ กำลังบันทึก...'; }

  const fname  = document.querySelector('#page-payment input[placeholder="ชื่อ"]')?.value.trim() || '';
  const lname  = document.querySelector('#page-payment input[placeholder="นามสกุล"]')?.value.trim() || '';
  const phone  = document.querySelector('#page-payment input[type="tel"]')?.value.trim() || '';
  const email  = document.querySelector('#page-payment input[type="email"]')?.value.trim() || '';
  const idCard = document.getElementById('idcard')?.value.trim() || '';

  if (!fname || !lname || !phone || !email) {
    toast('กรุณากรอกข้อมูลผู้เช่าให้ครบถ้วน (ชื่อ นามสกุล เบอร์โทร อีเมล)', 'warning');
    if (btn) { btn.disabled = false; btn.textContent = '✔ ยืนยันการจองและชำระเงิน'; }
    return;
  }
  
  if (!selectedCar) { 
    toast('เกิดข้อผิดพลาด: ไม่ได้เลือกรถ', 'error'); 
    if (btn) { btn.disabled = false; btn.textContent = '✔ ยืนยันการจองและชำระเงิน'; }
    return; 
  }

  const deliverySel = document.getElementById('delivery');
  let deliveryValue = deliverySel?.value || '';

  if (deliveryValue === 'custom-map') {
    const lat = deliverySel?.dataset.customLat;
    const lng = deliverySel?.dataset.customLng;
    if (lat && lng) {
      deliveryValue = `Latitude:${parseFloat(lat).toFixed(5)}Longitude:${parseFloat(lng).toFixed(5)}`;
    } else {
      alert('กรุณาปักหมุดตำแหน่งของคุณบนแผนที่ก่อน');
      if (btn) { btn.disabled = false; btn.textContent = '✔ ยืนยันการจองและชำระเงิน'; }
      return;
    }
  }

  const activeMethod = document.querySelector('.pay-method.selected');
  const methodType = activeMethod?.getAttribute('data-method');

  let payMethod  = '';
  let cardName   = '';
  let cardNumber = '';
  let timeOrCvv  = '';
  let expiry     = '';

  if (methodType === 'credit' || !methodType) {
    payMethod = 'Credit-Card';
    const sec = document.getElementById('credit-section');
    cardName   = sec?.querySelector('#card-name')?.value || '-';
    expiry     = sec?.querySelector('#card-exp')?.value || '-';
    timeOrCvv  = sec?.querySelector('#card-cvv')?.value || '-';
    cardNumber = sec?.querySelector('#card-num')?.value.replace(/\s+/g, '') || '-';
  } else if (methodType === 'qr') {
    payMethod = 'Promptpay';
    const sec = document.getElementById('qr-section');
    cardName   = sec?.querySelector('#card-name')?.value || '-';
    expiry     = sec?.querySelector('#card-exp')?.value || '-';
    timeOrCvv  = sec?.querySelector('#card-cvv')?.value || '-';
    cardNumber = sec?.querySelector('#card-num')?.value || '-';
  } else if (methodType === 'bank') {
    payMethod = 'Bank-Transfer';
    const sec = document.getElementById('bank-section');
    cardName   = sec?.querySelector('#card-name')?.value || '-';
    expiry     = sec?.querySelector('#card-exp')?.value || '-';
    timeOrCvv  = sec?.querySelector('#card-cvv')?.value || '-';
    cardNumber = sec?.querySelector('#card-num')?.value || '-';
  }

  const totalText = document.getElementById('sum-total')?.textContent || '';
  const totalString = totalText.replace(/,/g,'').replace(/฿/g,'').trim();
  const deliveryCostNum = parseInt((document.getElementById('sum-delivery')?.textContent || '0').replace(/,/g,'')) || 0;
  
  const totalNum = parseInt(totalString) || 0; 
  const rentCostNum = totalNum - deliveryCostNum - 3000;

  showSpinner('กำลังบันทึกการจอง', 'กรุณาอย่าปิดหน้าต่าง...');

  const safeStartDate = document.getElementById('start-date')?.value || startDate;
  const safeEndDate = document.getElementById('end-date')?.value || endDate;

  try {
    const alive = await API.isServerAlive();
    
    if (!alive) {
      const ref = 'RM-' + Math.floor(100000 + Math.random() * 900000);
      document.getElementById('booking-ref-num').textContent = ref;
      _showSuccessCarInfo({ fname, lname, phone, email, rentCost: rentCostNum, deliveryCost: deliveryCostNum, total: totalNum });
      paymentCompleted = true;
      showPage('success');
      hideSpinner();
      if (btn) { btn.disabled = false; btn.textContent = '✔ ยืนยันการจองและชำระเงิน'; }
      return;
    }

    const carNumber = selectedCar.serverNumber;
    
    if (!carNumber) {
      toast('เกิดข้อผิดพลาด: ไม่พบหมายเลขรถ', 'error');
      if (btn) { btn.disabled = false; btn.textContent = '✔ ยืนยันการจองและชำระเงิน'; }
      hideSpinner();
      return;
    }

    const result = await API.book({
      carNumber, 
      startDate: safeStartDate, 
      endDate: safeEndDate,
      firstName: fname, 
      lastName: lname,
      phone, 
      email, 
      idCard,
      deliveryValue,
      payMethod, 
      cardName, 
      cardNumber,
      timeOrCvv, 
      expiry, 
      total: totalNum
    });

    if (btn) { btn.disabled = false; btn.textContent = '✔ ยืนยันการจองและชำระเงิน'; }

    if (!result.ok) {
      hideSpinner();
      toast('ไม่สามารถจองได้: ' + (result.error || result.message || 'ข้อมูลไม่สมบูรณ์'), 'error');
      return;
    }

    document.getElementById('booking-ref-num').textContent = result.refCode;
    _showSuccessCarInfo({ fname, lname, phone, email, rentCost: rentCostNum, deliveryCost: deliveryCostNum, total: totalNum });
    paymentCompleted = true;
    hideSpinner();
    showPage('success');

  } catch (err) {
    if (btn) { btn.disabled = false; btn.textContent = '✔ ยืนยันการจองและชำระเงิน'; }
    hideSpinner();
    toast('ไม่สามารถเชื่อมต่อ server ได้ กรุณาลองใหม่', 'error');
  }
}

/* populate ข้อมูลรถบน success page */
function _showSuccessCarInfo({ fname='', lname='', phone='', email='', rentCost=0, deliveryCost=0, total=0 } = {}) {
  if (!selectedCar) return;
  const fmt = d => new Date(d + 'T00:00:00').toLocaleDateString('th-TH',
    { day:'numeric', month:'short', year:'numeric' });

  const imgEl   = document.getElementById('success-car-img');
  const nameEl  = document.getElementById('success-car-name');
  const dateEl  = document.getElementById('success-car-dates');
  const infoBox = document.getElementById('success-car-info');

  if (imgEl)   { imgEl.src = selectedCar.image || ''; imgEl.alt = selectedCar.name; }
  if (nameEl)  nameEl.textContent = selectedCar.name;
  if (dateEl)  dateEl.textContent = `📅 ${fmt(startDate)} → ${fmt(endDate)}  ( ${numDays} วัน )`;
  if (infoBox) infoBox.style.display = 'flex';

  // ข้อมูลผู้เช่า
  const fullnameEl = document.getElementById('success-fullname');
  const phoneEl    = document.getElementById('success-phone');
  const emailEl    = document.getElementById('success-email');
  if (fullnameEl) fullnameEl.textContent = `${fname} ${lname}`;
  if (phoneEl)    phoneEl.textContent    = phone;
  if (emailEl)    emailEl.textContent    = email;

  // สรุปราคา
  const rentEl     = document.getElementById('success-rent-cost');
  const deliverEl  = document.getElementById('success-delivery-cost');
  const totalEl    = document.getElementById('success-total-cost');
  if (rentEl)    rentEl.textContent    = rentCost.toLocaleString() + ' ฿';
  if (deliverEl) deliverEl.textContent = deliveryCost.toLocaleString() + ' ฿';
  if (totalEl)   totalEl.textContent   = total.toLocaleString() + ' ฿';
}

/* แสดง banner แจ้งสถานะ server เมื่อโหลดหน้า */
window.addEventListener('load', async () => {
  const alive = await API.isServerAlive();
  const banner = document.createElement('div');
  banner.id = 'server-status-banner';

  if (alive) {
    banner.style.cssText =
      'position:fixed;bottom:16px;right:16px;z-index:999;' +
      'background:rgba(74,222,128,0.12);border:1px solid rgba(74,222,128,0.35);' +
      'color:#4ade80;padding:10px 18px;border-radius:10px;font-size:13px;' +
      'backdrop-filter:blur(8px);';
    banner.textContent = '🟢 ระบบฐานข้อมูลเชื่อมต่อสำเร็จ';
  } else {
    banner.style.cssText =
      'position:fixed;bottom:16px;right:16px;z-index:999;' +
      'background:rgba(255,107,53,0.12);border:1px solid rgba(255,107,53,0.35);' +
      'color:#ff6b35;padding:10px 18px;border-radius:10px;font-size:13px;' +
      'backdrop-filter:blur(8px);';
    banner.textContent = '🔴 ไม่สามารถเชื่อมต่อ server (offline mode)';
  }

  document.body.appendChild(banner);
  setTimeout(() => banner.style.opacity='0', 5000);
  setTimeout(() => banner.remove(), 5500);
});
