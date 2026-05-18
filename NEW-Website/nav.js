/* nav.js — shared hamburger menu logic */ 
/* test add friends*/
(function() {
  function initNav() {
    const nav = document.querySelector('nav');
    if (!nav) return;

    /* inject hamburger button */
    const burger = document.createElement('button');
    burger.className = 'nav-hamburger';
    burger.setAttribute('aria-label', 'เปิดเมนู');
    burger.innerHTML = '<span></span><span></span><span></span>';
    nav.appendChild(burger);

    /* clone nav links into drawer */
    const origUl = nav.querySelector('ul');
    const drawer = document.createElement('div');
    drawer.className = 'nav-drawer';
    const drawerUl = origUl.cloneNode(true);
    drawer.appendChild(drawerUl);
    document.body.insertBefore(drawer, document.body.firstChild.nextSibling || document.body.firstChild);

    /* scrim overlay */
    const scrim = document.createElement('div');
    scrim.className = 'nav-scrim';
    document.body.appendChild(scrim);

    /* close all drawer links on click — also guard success page */
    drawerUl.querySelectorAll('a').forEach(a => {
      a.addEventListener('click', function(e) {
        /* ถ้า success กำลังแสดงอยู่ ห้าม navigate ออกจาก drawer */
        if (typeof paymentCompleted !== 'undefined' && paymentCompleted) {
          const href = a.getAttribute('href');
          if (href && href !== '#' && !href.startsWith('javascript')) {
            e.preventDefault();
            e.stopPropagation();
            alert('กรุณาจดหมายเลขการจองไว้ก่อน\nแล้วกดปุ่ม "กลับสู่หน้าหลัก" เพื่อออกจากหน้านี้');
            closeMenu();
            return;
          }
        }
        closeMenu();
      });
    });

    function openMenu() {
      burger.classList.add('open');
      drawer.classList.add('open');
      scrim.classList.add('visible');
      burger.setAttribute('aria-label', 'ปิดเมนู');
      document.body.style.overflow = 'hidden';
    }
    function closeMenu() {
      burger.classList.remove('open');
      drawer.classList.remove('open');
      scrim.classList.remove('visible');
      burger.setAttribute('aria-label', 'เปิดเมนู');
      document.body.style.overflow = '';
    }
    function toggleMenu() {
      burger.classList.contains('open') ? closeMenu() : openMenu();
    }

    burger.addEventListener('click', toggleMenu);
    scrim.addEventListener('click', closeMenu);

    /* close on ESC */
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape') closeMenu();
    });

    /* close on resize to desktop */
    window.addEventListener('resize', () => {
      if (window.innerWidth >= 768) closeMenu();
    });
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', initNav);
  else
    initNav();
})();
