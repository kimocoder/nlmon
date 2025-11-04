// nlmon Dashboard Application

class NlmonDashboard {
    constructor() {
        this.ws = null;
        this.events = [];
        this.maxEvents = 1000;
        this.autoScroll = true;
        this.eventRate = 0;
        this.lastEventCount = 0;
        this.lastEventTime = Date.now();
        
        this.init();
    }
    
    init() {
        this.setupTabs();
        this.setupToolbar();
        this.connectWebSocket();
        this.loadInitialData();
        this.startStatsUpdate();
    }
    
    setupTabs() {
        const tabButtons = document.querySelectorAll('.tab-button');
        const tabContents = document.querySelectorAll('.tab-content');
        
        tabButtons.forEach(button => {
            button.addEventListener('click', () => {
                const tabName = button.dataset.tab;
                
                tabButtons.forEach(btn => btn.classList.remove('active'));
                tabContents.forEach(content => content.classList.remove('active'));
                
                button.classList.add('active');
                document.getElementById(`${tabName}-tab`).classList.add('active');
                
                if (tabName === 'stats') {
                    this.updateStats();
                } else if (tabName === 'filters') {
                    this.loadFilters();
                } else if (tabName === 'config') {
                    this.loadConfig();
                } else if (tabName === 'alerts') {
                    this.loadAlerts();
                }
            });
        });
    }
    
    setupToolbar() {
        document.getElementById('search-input').addEventListener('input', (e) => {
            this.filterEvents(e.target.value);
        });
        
        document.getElementById('event-type-filter').addEventListener('change', (e) => {
            this.filterEventsByType(e.target.value);
        });
        
        document.getElementById('clear-events').addEventListener('click', () => {
            this.clearEvents();
        });
        
        document.getElementById('export-events').addEventListener('click', () => {
            this.exportEvents();
        });
        
        document.getElementById('auto-scroll').addEventListener('change', (e) => {
            this.autoScroll = e.target.checked;
        });
        
        document.getElementById('add-filter').addEventListener('click', () => {
            this.showFilterEditor();
        });
        
        document.getElementById('cancel-filter').addEventListener('click', () => {
            this.hideFilterEditor();
        });
        
        document.getElementById('filter-form').addEventListener('submit', (e) => {
            e.preventDefault();
            this.saveFilter();
        });
        
        document.getElementById('save-config').addEventListener('click', () => {
            this.saveConfig();
        });
        
        document.getElementById('reload-config').addEventListener('click', () => {
            this.loadConfig();
        });
    }
    
    connectWebSocket() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.hostname}:${window.location.port}/ws`;
        
        this.ws = new WebSocket(wsUrl);
        
        this.ws.onopen = () => {
            console.log('WebSocket connected');
            this.updateConnectionStatus(true);
            this.ws.send(JSON.stringify({ type: 'subscribe', filters: [] }));
        };
        
        this.ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            
            if (data.type === 'event') {
                this.addEvent(data.data);
            } else if (data.type === 'stats') {
                this.updateStatsData(data.data);
            }
        };
        
        this.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };
        
        this.ws.onclose = () => {
            console.log('WebSocket disconnected');
            this.updateConnectionStatus(false);
            setTimeout(() => this.connectWebSocket(), 5000);
        };
    }
    
    updateConnectionStatus(connected) {
        const badge = document.getElementById('connection-status');
        if (connected) {
            badge.textContent = 'Connected';
            badge.classList.remove('disconnected');
            badge.classList.add('connected');
        } else {
            badge.textContent = 'Disconnected';
            badge.classList.remove('connected');
            badge.classList.add('disconnected');
        }
    }
    
    addEvent(event) {
        this.events.unshift(event);
        
        if (this.events.length > this.maxEvents) {
            this.events.pop();
        }
        
        this.renderEvent(event);
        this.updateEventRate();
    }
    
    renderEvent(event) {
        const tbody = document.getElementById('events-body');
        const row = document.createElement('tr');
        
        const timestamp = new Date(event.timestamp).toLocaleString();
        const eventType = event.type || 'unknown';
        
        row.innerHTML = `
            <td>${timestamp}</td>
            <td><span class="event-type ${eventType}">${eventType}</span></td>
            <td>${event.interface || '-'}</td>
            <td>${event.message_type || '-'}</td>
            <td>${this.formatDetails(event.details)}</td>
        `;
        
        row.addEventListener('click', () => {
            this.showEventDetails(event);
        });
        
        tbody.insertBefore(row, tbody.firstChild);
        
        if (tbody.children.length > this.maxEvents) {
            tbody.removeChild(tbody.lastChild);
        }
        
        if (this.autoScroll) {
            const container = document.querySelector('.events-container');
            container.scrollTop = 0;
        }
    }
    
    formatDetails(details) {
        if (!details) return '-';
        if (typeof details === 'string') return details;
        return JSON.stringify(details).substring(0, 50) + '...';
    }
    
    showEventDetails(event) {
        alert(JSON.stringify(event, null, 2));
    }
    
    updateEventRate() {
        const now = Date.now();
        const timeDiff = (now - this.lastEventTime) / 1000;
        
        if (timeDiff >= 1) {
            const eventDiff = this.events.length - this.lastEventCount;
            this.eventRate = (eventDiff / timeDiff).toFixed(1);
            
            document.getElementById('event-rate').textContent = `${this.eventRate} events/s`;
            
            this.lastEventCount = this.events.length;
            this.lastEventTime = now;
        }
    }
    
    filterEvents(searchTerm) {
        const rows = document.querySelectorAll('#events-body tr');
        const term = searchTerm.toLowerCase();
        
        rows.forEach(row => {
            const text = row.textContent.toLowerCase();
            row.style.display = text.includes(term) ? '' : 'none';
        });
    }
    
    filterEventsByType(type) {
        const rows = document.querySelectorAll('#events-body tr');
        
        rows.forEach(row => {
            if (!type) {
                row.style.display = '';
            } else {
                const eventType = row.querySelector('.event-type');
                row.style.display = eventType.classList.contains(type) ? '' : 'none';
            }
        });
    }
    
    clearEvents() {
        this.events = [];
        document.getElementById('events-body').innerHTML = '';
    }
    
    exportEvents() {
        const data = JSON.stringify(this.events, null, 2);
        const blob = new Blob([data], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `nlmon-events-${Date.now()}.json`;
        a.click();
        URL.revokeObjectURL(url);
    }
    
    async loadInitialData() {
        try {
            const response = await fetch('/api/events?limit=100');
            const data = await response.json();
            
            data.events.forEach(event => {
                this.renderEvent(event);
            });
        } catch (error) {
            console.error('Failed to load initial events:', error);
        }
    }
    
    async updateStats() {
        try {
            const response = await fetch('/api/stats');
            const data = await response.json();
            
            this.updateStatsData(data.stats);
        } catch (error) {
            console.error('Failed to load stats:', error);
        }
    }
    
    updateStatsData(stats) {
        document.getElementById('total-events').textContent = stats.total_events || 0;
        document.getElementById('event-rate-stat').textContent = `${stats.event_rate || 0}/s`;
        
        const memoryMB = Math.round((stats.memory_usage?.rss || 0) / 1024 / 1024);
        document.getElementById('memory-usage').textContent = `${memoryMB} MB`;
        
        document.getElementById('cpu-usage').textContent = `${stats.cpu_usage || 0}%`;
    }
    
    startStatsUpdate() {
        setInterval(() => {
            if (document.getElementById('stats-tab').classList.contains('active')) {
                this.updateStats();
            }
        }, 5000);
    }
    
    async loadFilters() {
        try {
            const response = await fetch('/api/filters');
            const data = await response.json();
            
            const container = document.getElementById('filters-list');
            container.innerHTML = '';
            
            data.filters.forEach(filter => {
                const item = document.createElement('div');
                item.className = 'filter-item';
                item.innerHTML = `
                    <div class="filter-info">
                        <h3>${filter.name}</h3>
                        <code>${filter.expression}</code>
                    </div>
                    <div class="filter-actions">
                        <button onclick="dashboard.toggleFilter(${filter.id})">
                            ${filter.enabled ? 'Disable' : 'Enable'}
                        </button>
                        <button class="delete" onclick="dashboard.deleteFilter(${filter.id})">
                            Delete
                        </button>
                    </div>
                `;
                container.appendChild(item);
            });
        } catch (error) {
            console.error('Failed to load filters:', error);
        }
    }
    
    showFilterEditor() {
        document.getElementById('filter-editor').style.display = 'flex';
    }
    
    hideFilterEditor() {
        document.getElementById('filter-editor').style.display = 'none';
        document.getElementById('filter-form').reset();
    }
    
    async saveFilter() {
        const name = document.getElementById('filter-name').value;
        const expression = document.getElementById('filter-expression').value;
        const enabled = document.getElementById('filter-enabled').checked;
        
        try {
            const response = await fetch('/api/filters', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name, expression, enabled })
            });
            
            if (response.ok) {
                this.hideFilterEditor();
                this.loadFilters();
            }
        } catch (error) {
            console.error('Failed to save filter:', error);
        }
    }
    
    async loadConfig() {
        try {
            const response = await fetch('/api/config');
            const data = await response.json();
            
            document.getElementById('config-text').value = 
                JSON.stringify(data.config, null, 2);
        } catch (error) {
            console.error('Failed to load config:', error);
        }
    }
    
    async saveConfig() {
        const configText = document.getElementById('config-text').value;
        
        try {
            const config = JSON.parse(configText);
            const response = await fetch('/api/config', {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            });
            
            if (response.ok) {
                alert('Configuration saved successfully');
            }
        } catch (error) {
            console.error('Failed to save config:', error);
            alert('Failed to save configuration: ' + error.message);
        }
    }
    
    async loadAlerts() {
        try {
            const response = await fetch('/api/alerts');
            const data = await response.json();
            
            const container = document.getElementById('alerts-list');
            container.innerHTML = '';
            
            data.alerts.forEach(alert => {
                const item = document.createElement('div');
                item.className = `alert-item ${alert.status}`;
                
                const timestamp = new Date(alert.timestamp * 1000).toLocaleString();
                
                item.innerHTML = `
                    <div class="alert-header">
                        <h3>${alert.name}</h3>
                        <span class="alert-time">${timestamp}</span>
                    </div>
                    <div class="alert-details">${alert.details}</div>
                    <div class="alert-actions">
                        ${alert.status === 'active' ? 
                            `<button onclick="dashboard.acknowledgeAlert(${alert.id})">Acknowledge</button>` : 
                            '<span>Acknowledged</span>'}
                    </div>
                `;
                container.appendChild(item);
            });
        } catch (error) {
            console.error('Failed to load alerts:', error);
        }
    }
    
    async acknowledgeAlert(alertId) {
        try {
            const response = await fetch('/api/alerts/acknowledge', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: alertId })
            });
            
            if (response.ok) {
                this.loadAlerts();
            }
        } catch (error) {
            console.error('Failed to acknowledge alert:', error);
        }
    }
}

// Initialize dashboard
const dashboard = new NlmonDashboard();
