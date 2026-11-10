/* =========================================================
   MEDS KEYCHAIN - WEBSITE CLOUD API CONFIGURATION
   ========================================================= */

/*
  Replace this empty string with your friend's backend URL
  once the backend is ready.

  Example:
  const API_BASE_URL = 'https://your-backend-url.com';
*/
const API_BASE_URL = window.location.origin;

/*
  These are the API routes expected by this website.
  They must match your friend's backend implementation.
*/
const API = {
  health: '/api/health',
  medications: '/api/medications',
  doseHistory: '/api/dose-history'
};

/* =========================================================
   LOCAL DATA
   ========================================================= */

let meds = JSON.parse(localStorage.getItem('meds') || '[]');
let pendingDeletes = JSON.parse(localStorage.getItem('pendingDeletes') || '[]');
let history = JSON.parse(localStorage.getItem('history') || '[]');
let lastSync = localStorage.getItem('lastSync') || null;

let editingMedicationId = null;
let cloudConnected = false;
let syncInProgress = false;

function saveLocalData() {
  localStorage.setItem('meds', JSON.stringify(meds));
  localStorage.setItem('pendingDeletes', JSON.stringify(pendingDeletes));
  localStorage.setItem('history', JSON.stringify(history));
  localStorage.setItem('lastSync', lastSync || '');
}

/* =========================================================
   API HELPERS
   ========================================================= */

function apiConfigured() {
  return API_BASE_URL.trim().length > 0;
}

function apiUrl(path) {
  return API_BASE_URL.replace(/\/$/, '') + path;
}

async function apiRequest(path, options = {}) {
  if (!apiConfigured()) {
    throw new Error('Backend URL has not been configured.');
  }

  const response = await fetch(apiUrl(path), {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...(options.headers || {})
    }
  });

  if (!response.ok) {
    const errorText = await response.text().catch(() => '');
    throw new Error(
      `API error ${response.status}: ${errorText || response.statusText}`
    );
  }

  if (response.status === 204) return null;

  const contentType = response.headers.get('content-type') || '';

  if (contentType.includes('application/json')) {
    return response.json();
  }

  return null;
}

/* =========================================================
   CONNECTION STATUS
   ========================================================= */

function setCloudStatus(connected, message = '') {
  cloudConnected = connected;

  const headerDot = document.getElementById('headerStatusDot');
  const headerText = document.getElementById('headerStatusText');
  const deviceDot = document.getElementById('deviceStatusDot');
  const deviceText = document.getElementById('deviceStatusText');
  const connectionInfo = document.getElementById('connectionInfo');

  headerDot.className = 'status-dot' + (connected ? ' connected' : '');
  deviceDot.className = 'status-dot' + (connected ? ' connected' : '');

  if (!apiConfigured()) {
    headerText.textContent = 'Local mode';
    deviceText.textContent = 'Backend not configured';
    connectionInfo.textContent = 'Local only';
  } else if (connected) {
    headerText.textContent = 'Cloud connected';
    deviceText.textContent = 'Cloud backend connected';
    connectionInfo.textContent = 'Connected';
  } else {
    headerText.textContent = 'Offline';
    deviceText.textContent = message || 'Unable to connect';
    connectionInfo.textContent = 'Disconnected';
  }
}

async function checkConnection() {
  if (!apiConfigured()) {
    setCloudStatus(false);
    showToast('Backend URL has not been configured yet.');
    return false;
  }

  try {
    await apiRequest(API.health);
    setCloudStatus(true);
    showToast('Cloud backend connected!');
    return true;
  } catch (error) {
    setCloudStatus(false, 'Connection failed');
    showToast('Unable to connect to backend.');
    console.error('Connection check failed:', error);
    return false;
  }
}

/* =========================================================
   DATA NORMALISATION
   ========================================================= */

function normaliseMedication(item) {
  return {
    id: String(item.id || item._id || item.medicationId || Date.now()),
    name: String(item.name || item.medicationName || ''),
    time: String(item.time || item.reminderTime || '08:00'),
    days: Array.isArray(item.days)
      ? item.days.map(Number)
      : [0,1,2,3,4,5,6],
    message: String(item.message || item.reminderMessage || 'Time for meds!')
  };
}

function normaliseHistory(item) {
  return {
    id: String(item.id || item._id || Date.now()),
    medicationId: String(item.medicationId || ''),
    medicationName: String(item.medicationName || item.name || 'Medication'),
    timestamp: item.timestamp || item.takenAt || new Date().toISOString(),
    status: item.status || 'taken'
  };
}

/* =========================================================
   MEDICATION CLOUD OPERATIONS
   ========================================================= */

async function uploadMedication(medication) {
  const path = `${API.medications}/${encodeURIComponent(medication.id)}`;

  return apiRequest(path, {
    method: 'PUT',
    body: JSON.stringify(medication)
  });
}

async function deleteMedicationFromCloud(id) {
  const path = `${API.medications}/${encodeURIComponent(id)}`;

  return apiRequest(path, {
    method: 'DELETE'
  });
}

async function fetchMedicationsFromCloud() {
  const result = await apiRequest(API.medications);

  if (Array.isArray(result)) return result;

  if (result && Array.isArray(result.medications)) {
    return result.medications;
  }

  return [];
}

async function fetchHistoryFromCloud() {
  const result = await apiRequest(API.doseHistory);

  if (Array.isArray(result)) return result;

  if (result && Array.isArray(result.history)) {
    return result.history;
  }

  return [];
}

/* =========================================================
   CLOUD SYNCHRONISATION
   ========================================================= */

async function sync() {
  if (syncInProgress) return;

  if (!apiConfigured()) {
    setCloudStatus(false);
    showToast('Add your backend URL before syncing.');
    return;
  }

  syncInProgress = true;

  const syncButton = document.getElementById('syncButton');
  syncButton.disabled = true;
  syncButton.textContent = 'Syncing...';

  try {
    // Check backend connection.
    await apiRequest(API.health);

    // Process medication deletions waiting to sync.
    for (const id of [...pendingDeletes]) {
      await deleteMedicationFromCloud(id);
      pendingDeletes = pendingDeletes.filter(item => item !== id);
      saveLocalData();
    }

    // Upload local medication information.
    for (const medication of meds) {
      await uploadMedication(medication);
    }

    // Download the latest medication information.
    const cloudMedications = await fetchMedicationsFromCloud();

    meds = cloudMedications.map(normaliseMedication);

    // Download medication history.
    const cloudHistory = await fetchHistoryFromCloud();

    history = cloudHistory.map(normaliseHistory);

    lastSync = new Date().toISOString();

    saveLocalData();
    setCloudStatus(true);

    render();
    showToast('Synchronisation successful!');
  } catch (error) {
    console.error('Synchronisation error:', error);
    setCloudStatus(false, 'Sync failed');
    showToast('Sync failed. Please check your connection.');
  } finally {
    syncInProgress = false;
    syncButton.disabled = false;
    syncButton.textContent = 'Sync now';
    updateDeviceInformation();
  }
}

/* =========================================================
   AUTOMATIC CLOUD UPLOAD AFTER MEDICATION CHANGES
   ========================================================= */

async function syncMedicationAfterChange(medication) {
  if (!apiConfigured()) return;

  try {
    await uploadMedication(medication);
    setCloudStatus(true);
    lastSync = new Date().toISOString();
    saveLocalData();
    updateDeviceInformation();
  } catch (error) {
    console.error('Medication upload failed:', error);
    showToast('Saved locally. Cloud upload failed.');
    setCloudStatus(false, 'Upload failed');
  }
}

/* =========================================================
   NAVIGATION
   ========================================================= */

function showPage(page, button) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));

  document.getElementById('page-' + page).classList.add('active');

  if (button) button.classList.add('active');

  if (page === 'device') updateDeviceInformation();
  if (page === 'home') renderHome();
  if (page === 'medicines') renderMedications();
  if (page === 'history') renderHistory();
}

/* =========================================================
   TOAST
   ========================================================= */

let toastTimeout;

function showToast(message) {
  const toast = document.getElementById('toast');
  toast.textContent = message;
  toast.classList.add('show');

  clearTimeout(toastTimeout);

  toastTimeout = setTimeout(() => {
    toast.classList.remove('show');
  }, 2500);
}

/* =========================================================
   DATE AND TIME HELPERS
   ========================================================= */

function getTodayKey() {
  const now = new Date();

  return [
    now.getFullYear(),
    String(now.getMonth() + 1).padStart(2, '0'),
    String(now.getDate()).padStart(2, '0')
  ].join('-');
}

function formatTime(time) {
  if (!time) return '';

  const [hours, minutes] = time.split(':').map(Number);
  const date = new Date();

  date.setHours(hours, minutes, 0, 0);

  return date.toLocaleTimeString([], {
    hour: 'numeric',
    minute: '2-digit'
  });
}

function formatDateTime(timestamp) {
  const date = new Date(timestamp);

  if (Number.isNaN(date.getTime())) return 'Unknown date';

  return date.toLocaleString([], {
    dateStyle: 'medium',
    timeStyle: 'short'
  });
}

function getDayName(day) {
  return ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'][day];
}

function medicationOccursToday(medication) {
  return Array.isArray(medication.days) &&
    medication.days.includes(new Date().getDay());
}

function isTakenToday(medication) {
  const today = getTodayKey();

  return history.some(entry => {
    const entryDate = new Date(entry.timestamp);

    return entry.medicationId === medication.id &&
      getTodayKeyFromDate(entryDate) === today &&
      entry.status === 'taken';
  });
}

function getTodayKeyFromDate(date) {
  return [
    date.getFullYear(),
    String(date.getMonth() + 1).padStart(2, '0'),
    String(date.getDate()).padStart(2, '0')
  ].join('-');
}

/* =========================================================
   HOME DASHBOARD
   ========================================================= */

function renderHome() {
  const now = new Date();

  document.getElementById('todayDate').textContent =
    now.toLocaleDateString([], {
      weekday:'long',
      day:'numeric',
      month:'long',
      year:'numeric'
    });

  const todaysMeds = meds
    .filter(medicationOccursToday)
    .sort((a,b) => a.time.localeCompare(b.time));

  const taken = todaysMeds.filter(isTakenToday).length;

  document.getElementById('scheduledCount').textContent = todaysMeds.length;
  document.getElementById('takenCount').textContent = taken;

  const progress = todaysMeds.length
    ? Math.round((taken / todaysMeds.length) * 100)
    : 0;

  document.getElementById('progressPercent').textContent = progress + '%';
  document.getElementById('progressFill').style.width = progress + '%';

  const nextMedication = todaysMeds.find(medication => {
    if (isTakenToday(medication)) return false;

    const [hours,minutes] = medication.time.split(':').map(Number);

    const scheduled = new Date();
    scheduled.setHours(hours,minutes,0,0);

    return scheduled >= now;
  });

  const nextName = document.getElementById('nextMedicationName');
  const nextTime = document.getElementById('nextMedicationTime');
  const countdown = document.getElementById('nextCountdown');

  if (nextMedication) {
    nextName.textContent = nextMedication.name;
    nextTime.textContent = formatTime(nextMedication.time);

    const [hours,minutes] = nextMedication.time.split(':').map(Number);
    const scheduled = new Date();

    scheduled.setHours(hours,minutes,0,0);

    const difference = scheduled - now;
    const totalMinutes = Math.max(0, Math.floor(difference / 60000));
    const h = Math.floor(totalMinutes / 60);
    const m = totalMinutes % 60;

    countdown.textContent = `In ${h} hours and ${m} minutes`;
  } else {
    nextName.textContent = 'No upcoming medication';
    nextTime.textContent = todaysMeds.length
      ? 'You have completed or passed today’s reminders.'
      : 'Add a medication to get started';
    countdown.textContent = '';
  }

  const list = document.getElementById('todayMedicationList');

  if (!todaysMeds.length) {
    list.innerHTML = '<div class="empty">No medications scheduled for today.</div>';
  } else {
    list.innerHTML = todaysMeds.map(medication => `
      <div class="med-row">
        <div>
          <div class="med-name">${escapeHTML(medication.name)}</div>
          <div class="med-detail">${formatTime(medication.time)}</div>
        </div>
        <span class="pill ${isTakenToday(medication) ? 'taken' : 'pending'}">
          ${isTakenToday(medication) ? 'Taken' : 'Pending'}
        </span>
      </div>
    `).join('');
  }
}

/* =========================================================
   MEDICATION LIST
   ========================================================= */

function renderMedications() {
  const list = document.getElementById('medicationList');

  if (!meds.length) {
    list.innerHTML = '<div class="empty">No medications added yet.</div>';
    return;
  }

  list.innerHTML = meds.map(medication => `
    <div class="med-row">
      <div style="flex:1;min-width:0">
        <div class="med-name">${escapeHTML(medication.name)}</div>
        <div class="med-detail">
          ${formatTime(medication.time)} ·
          ${medication.days.map(getDayName).join(', ')}
        </div>
        <div class="med-detail">${escapeHTML(medication.message)}</div>
      </div>
      <div style="display:flex;gap:6px;align-items:center">
        <button class="btn secondary" style="padding:8px 10px"
          onclick="editMedication('${escapeJS(medication.id)}')">Edit</button>
        <button class="btn danger" style="padding:8px 10px"
          onclick="deleteMedication('${escapeJS(medication.id)}')">×</button>
      </div>
    </div>
  `).join('');
}

/* =========================================================
   MEDICATION MODAL
   ========================================================= */

function openMedicationModal() {
  editingMedicationId = null;

  document.getElementById('modalTitle').textContent = 'Add medication';
  document.getElementById('medName').value = '';
  document.getElementById('medTime').value = '08:00';
  document.getElementById('medMessage').value = 'Time for meds!';

  document.querySelectorAll('.day-btn').forEach(button => {
    button.classList.add('selected');
  });

  document.getElementById('medicationModal').classList.add('show');
}

function closeMedicationModal() {
  document.getElementById('medicationModal').classList.remove('show');
}

function toggleDay(button) {
  button.classList.toggle('selected');
}

function editMedication(id) {
  const medication = meds.find(item => item.id === id);

  if (!medication) return;

  editingMedicationId = id;

  document.getElementById('modalTitle').textContent = 'Edit medication';
  document.getElementById('medName').value = medication.name;
  document.getElementById('medTime').value = medication.time;
  document.getElementById('medMessage').value = medication.message;

  document.querySelectorAll('.day-btn').forEach(button => {
    const day = Number(button.dataset.day);

    button.classList.toggle('selected', medication.days.includes(day));
  });

  document.getElementById('medicationModal').classList.add('show');
}

function saveMedication() {
  const name = document.getElementById('medName').value.trim();
  const time = document.getElementById('medTime').value;
  const message = document.getElementById('medMessage').value.trim();

  const days = [...document.querySelectorAll('.day-btn.selected')]
    .map(button => Number(button.dataset.day));

  if (!name) {
    showToast('Please enter a medication name.');
    return;
  }

  if (!time) {
    showToast('Please select a reminder time.');
    return;
  }

  if (!days.length) {
    showToast('Please select at least one day.');
    return;
  }

  const medication = {
    id: editingMedicationId || (
      crypto.randomUUID
        ? crypto.randomUUID()
        : String(Date.now())
    ),
    name,
    time,
    days,
    message: message || 'Time for meds!'
  };

  if (editingMedicationId) {
    meds = meds.map(item =>
      item.id === editingMedicationId ? medication : item
    );
  } else {
    meds.push(medication);
  }

  saveLocalData();
  closeMedicationModal();
  render();

  showToast(editingMedicationId
    ? 'Medication updated.'
    : 'Medication added.');

  syncMedicationAfterChange(medication);
}

/* =========================================================
   DELETE MEDICATION
   ========================================================= */

async function deleteMedication(id) {
  const medication = meds.find(item => item.id === id);

  if (!medication) return;

  if (!confirm(`Delete ${medication.name}?`)) return;

  meds = meds.filter(item => item.id !== id);

  if (!pendingDeletes.includes(id)) {
    pendingDeletes.push(id);
  }

  saveLocalData();
  render();

  showToast('Medication deleted.');

  if (apiConfigured()) {
    try {
      await deleteMedicationFromCloud(id);

      pendingDeletes = pendingDeletes.filter(item => item !== id);

      saveLocalData();
    } catch (error) {
      console.error('Cloud deletion failed:', error);
      showToast('Deleted locally. Cloud deletion is pending.');
    }
  }
}

/* =========================================================
   HISTORY
   ========================================================= */

function renderHistory() {
  const list = document.getElementById('historyList');

  if (!history.length) {
    list.innerHTML = '<div class="empty">No medication history yet.</div>';
    return;
  }

  const sortedHistory = [...history].sort(
    (a,b) => new Date(b.timestamp) - new Date(a.timestamp)
  );

  list.innerHTML = sortedHistory.map(entry => `
    <div class="med-row">
      <div>
        <div class="med-name">${escapeHTML(entry.medicationName)}</div>
        <div class="med-detail">${formatDateTime(entry.timestamp)}</div>
      </div>
      <span class="pill ${entry.status === 'taken' ? 'taken' : 'missed'}">
        ${escapeHTML(entry.status)}
      </span>
    </div>
  `).join('');
}

/* =========================================================
   RECORD DOSE CONFIRMATION
   ========================================================= */

async function recordDoseTaken(medicationId) {
  const medication = meds.find(item => item.id === medicationId);

  if (!medication) return;

  const entry = {
    id: crypto.randomUUID
      ? crypto.randomUUID()
      : String(Date.now()),
    medicationId: medication.id,
    medicationName: medication.name,
    timestamp: new Date().toISOString(),
    status: 'taken'
  };

  history.push(entry);

  saveLocalData();
  render();

  if (apiConfigured()) {
    try {
      await apiRequest(API.doseHistory, {
        method: 'POST',
        body: JSON.stringify(entry)
      });
    } catch (error) {
      console.error('Dose confirmation upload failed:', error);
      showToast('Dose saved locally. Cloud upload failed.');
    }
  }
}

/* =========================================================
   DEVICE INFORMATION
   ========================================================= */

function updateDeviceInformation() {
  document.getElementById('deviceMedCount').textContent = meds.length;

  document.getElementById('lastSyncInfo').textContent = lastSync
    ? formatDateTime(lastSync)
    : 'Never';
}

/* =========================================================
   HTML ESCAPING
   ========================================================= */

function escapeHTML(value) {
  return String(value).replace(/[&<>"']/g, character => ({
    '&':'&amp;',
    '<':'&lt;',
    '>':'&gt;',
    '"':'&quot;',
    "'":'&#039;'
  })[character]);
}

function escapeJS(value) {
  return String(value).replace(/\\/g, '\\\\').replace(/'/g, "\\'");
}

/* =========================================================
   RENDER ALL SECTIONS
   ========================================================= */

function render() {
  renderHome();
  renderMedications();
  renderHistory();
  updateDeviceInformation();
}

/* =========================================================
   INITIALISE WEBSITE
   ========================================================= */

function initialiseApp() {
  render();

  if (apiConfigured()) {
    checkConnection();
  } else {
    setCloudStatus(false);
  }
}

initialiseApp();

/* Refresh dashboard countdown and clock periodically. */
setInterval(renderHome, 60000);

/* =========================================================
   OPTIONAL AUTOMATIC CLOUD CHECK
   ========================================================= */

/*
  This checks the backend periodically when configured.
  It does not automatically download all medication data;
  use the Sync now button to perform a full synchronisation.
*/
setInterval(() => {
  if (apiConfigured() && !syncInProgress) {
    checkConnection();
  }
}, 60000);
