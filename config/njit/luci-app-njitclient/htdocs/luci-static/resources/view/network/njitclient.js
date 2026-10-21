'use strict';
'require view';
'require rpc';
'require ui';
'require poll';
'require dom';

var getSettings = rpc.declare({ object: 'njitclient', method: 'get_settings', reject: true });
var getStatus = rpc.declare({ object: 'njitclient', method: 'status', reject: true });
var saveCredentials = rpc.declare({ object: 'njitclient', method: 'save', params: ['username', 'password', 'domain'], reject: true });
var saveOptions = rpc.declare({ object: 'njitclient', method: 'save_options', params: ['autostart', 'reconnect'], reject: true });
var clearCredentials = rpc.declare({ object: 'njitclient', method: 'clear', reject: true });
var control = rpc.declare({ object: 'njitclient', method: 'control', params: ['action'], reject: true });

var errors = {
	INVALID_INPUT: '输入格式或字节长度不符合要求。',
	STOP_BEFORE_SAVE: '请先停止认证，再修改或清除凭据。',
	PASSWORD_REQUIRED: '首次保存时必须填写密码。',
	SERVICE_UNAVAILABLE: '无法确认服务状态，已拒绝修改。',
	BUSY: '另一个管理操作正在进行，请稍后重试。',
	CONFIG_REQUIRED: '请先保存有效的认证凭据。',
	WAIT_FOR_STOP: '客户端尚未停止，请稍后点击开始认证。',
	CONTROL_FAILED: '服务操作未完成，请查看状态，不要连续重复认证。',
	SAVE_FAILED: '保存未完成，请重新读取确认。',
	CLEAR_FAILED: '清除未完成，请重新读取确认。'
};
var events = {
	starting: '客户端启动中', authenticating: '正在认证', authenticated: '最近收到认证成功事件',
	authentication_failed: '认证失败', credentials_rejected: '账号或密码被拒绝，等待人工检查',
	waiting_retry: '等待限速重试', interface_error: '认证接口异常', stopped: '已停止',
	waiting_link: '等待 WAN 链路', manual_required: '等待手动重新认证'
};
function checked(reply) {
	if (!reply || reply.ok !== true)
		throw new Error(errors[reply && reply.error] || '操作未完成，请检查服务状态。');
	return reply;
}
function row(label, input) {
	return E('div', { 'class': 'cbi-value' }, [E('label', { 'class': 'cbi-value-title' }, label),
		E('div', { 'class': 'cbi-value-field' }, [input])]);
}
return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,
	load: function() { return Promise.all([getSettings().then(checked), getStatus().then(checked)]); },
	writable: function() { return L.hasViewPermission() && location.protocol === 'https:'; },
	message: function(text, error) { ui.addNotification(null, E('p', {}, text), error ? 'error' : 'info'); },
	busy: function(value) {
		this.pending = value;
		this.buttons.forEach(function(b) { b.disabled = value || !L.hasViewPermission() || (b.dataset.tls === '1' && location.protocol !== 'https:'); });
	},
	refresh: function() {
		return getStatus().then(checked).then(L.bind(function(s) {
			var state = !s.service_known ? '服务状态未知' : s.running ? '运行中' : '已停止';
			var event = s.running && s.event ? (events[s.event] || '等待状态事件') : '无当前实例事件';
			if (s.event && typeof s.event_age_seconds === 'number') event += '（' + s.event_age_seconds + ' 秒前）';
			var lines = [
				['客户端', state], ['WAN 链路', !s.carrier_known ? '未知' : s.carrier ? '已连接' : '已断开'],
				['认证事件', event], ['凭据', s.configured ? '已配置' : '未配置或文件无效'],
				['WAN DHCP / IPv4', !s.wan_known ? '未知' : (s.wan_up ? '接口已就绪；' : '接口未就绪；') + (s.ipv4_addresses || []).join(', ')]
			];
			dom.content(this.statusBox, lines.map(function(r) { return E('p', {}, [E('strong', {}, r[0] + '：'), r[1]]); }));
		}, this)).catch(L.bind(function() { dom.content(this.statusBox, E('p', {}, '无法读取状态；不能据此认定认证成功。')); }, this));
	},
	perform: function(task, success) {
		if (this.pending) return Promise.resolve();
		this.busy(true);
		return Promise.resolve().then(task).then(checked).then(L.bind(function() { this.message(success, false); }, this))
			.catch(L.bind(function(e) { this.message(e.message || '操作失败', true); }, this))
			.finally(L.bind(function() { this.password.value = ''; this.busy(false); return this.refresh(); }, this));
	},
	confirm: function(title, text, task, success) {
		if (this.pending) return;
		ui.showModal(title, [E('p', {}, text), E('div', { 'class': 'right' }, [
			E('button', { 'class': 'btn', 'click': ui.hideModal }, '取消'), ' ',
			E('button', { 'class': 'btn cbi-button-action', 'click': L.bind(function() { ui.hideModal(); this.perform(task, success); }, this) }, '确认')
		])]);
	},
	button: function(label, action, tls) {
		var b = E('button', { 'class': 'btn cbi-button-action', 'type': 'button', 'data-tls': tls ? '1' : '0', 'click': action }, label);
		this.buttons.push(b); return b;
	},
	render: function(data) {
		var settings = data[0]; this.buttons = []; this.pending = false;
		this.username = E('input', { 'type': 'text', 'class': 'cbi-input-text', 'autocomplete': 'off', 'value': settings.username || '' });
		this.domain = E('input', { 'type': 'text', 'class': 'cbi-input-text', 'autocomplete': 'off', 'value': settings.domain || '' });
		this.password = E('input', { 'type': 'password', 'class': 'cbi-input-password', 'autocomplete': 'new-password', 'value': '', 'placeholder': settings.password_set ? '已设置；留空保留原密码' : '请输入认证密码' });
		this.autostart = E('input', { 'type': 'checkbox', 'checked': !!settings.autostart });
		this.reconnect = E('input', { 'type': 'checkbox', 'checked': !!settings.reconnect });
		[this.username, this.domain, this.password, this.autostart, this.reconnect].forEach(L.bind(function(input) { input.disabled = !this.writable(); }, this));
		this.statusBox = E('div', { 'class': 'cbi-section' }, '读取状态中…');
		var save = this.button('保存凭据', L.bind(function() {
			var username = this.username.value, domain = this.domain.value, password = this.password.value;
			var encoder = new TextEncoder();
			if (!username || encoder.encode(username + (domain ? '@' + domain : '')).length > 88 || encoder.encode(password).length > 111) {
				this.password.value = ''; this.message('用户名（含域）最多 88 字节，密码最多 111 字节。', true); return;
			}
			this.password.value = '';
			this.perform(function() { return saveCredentials(username, password, domain); }, '凭据已保存，未启动认证。');
		}, this), true);
		var options = this.button('保存自动认证选项', L.bind(function() {
			var a = this.autostart.checked, r = this.reconnect.checked;
			this.confirm('保存自动认证选项', '开机开关在下次启动时生效。断线自动认证开关立即影响运行中的重试；开启后，等待手动重试的客户端可能开始认证。账号或密码被拒绝时仍须人工检查。',
				function() { return saveOptions(a, r); }, '自动认证选项已保存。');
		}, this), true);
		var start = this.button('开始认证', L.bind(function() { this.confirm('开始认证', '请确认 WAN 已接入需要认证的真实网络。此操作会发送认证报文，不应用于临时管理网络。', function() { return control('start'); }, '已提交开始请求，请查看认证事件和 WAN 地址。'); }, this), true);
		var stop = this.button('停止认证', L.bind(function() { this.perform(function() { return control('stop'); }, '已提交停止请求，请核对状态。'); }, this), false);
		var restart = this.button('重新认证', L.bind(function() { this.confirm('重新认证', '将停止当前认证并重新发起认证；仅对 WAN 认证服务操作。', function() { return control('restart'); }, '已提交重新认证请求，请核对状态。'); }, this), true);
		var clear = this.button('清除认证凭据', L.bind(function() { this.confirm('清除认证凭据', '需先停止认证。确认后删除本机保存的账号、域和密码，自动认证选项保留。', function() { return clearCredentials(); }, '凭据已清除；再次认证前需重新保存。'); }, this), true);
		this.busy(false);
		poll.add(L.bind(this.refresh, this), 5);
		this.refresh();
		return E('div', {}, [
			E('h2', {}, 'njit 有线认证'),
			E('p', {}, '保存凭据不会启动或重启认证。客户端运行、认证事件和 WAN 地址分别显示，任一项都不能单独证明网络已稳定联网。'),
			location.protocol === 'https:' ? E('span') : E('p', { 'class': 'alert-message warning' }, '请先通过 HTTPS 打开管理页，再填写凭据、修改选项或开始认证；仍可在此停止认证。'),
			this.statusBox,
			E('div', { 'class': 'cbi-section' }, [E('h3', {}, '认证配置'), row('认证设备', E('span', {}, 'wan（固定 WAN 设备）')),
				row('用户名', this.username), row('账号域（可选）', this.domain), row('密码', this.password),
				E('p', {}, [save, ' ', clear]), E('p', {}, '修改凭据前请先停止认证；密码不会从后台回传。')]),
			E('div', { 'class': 'cbi-section' }, [E('h3', {}, '自动认证'), row('开机自动认证', this.autostart), row('断线后自动认证', this.reconnect),
				E('p', {}, '默认均关闭。断线恢复覆盖可检测的链路断开、服务器认证失败和抓包接口异常；静默的上级网络故障仍需实际测试。'), options]),
			E('div', { 'class': 'cbi-section' }, [start, ' ', stop, ' ', restart])
		]);
	}
});
