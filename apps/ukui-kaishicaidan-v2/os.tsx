import React, { useState } from 'react';
import { 
  Search, Power, Settings, User, FileText, Monitor, 
  LayoutGrid, Terminal, Folder, Globe, Mail, MessageSquare, 
  ChevronRight, Calculator, Calendar, Camera, Music, Video,
  Cpu, Shield
} from 'lucide-react';

export default function KylinStartMenu() {
  const [activeTab, setActiveTab] = useState('pinned'); // 'pinned' | 'all'

  const pinnedApps = [
    { name: '麒麟浏览器', icon: Globe, color: 'text-blue-400', bg: 'bg-blue-400/10' },
    { name: '终端', icon: Terminal, color: 'text-gray-300', bg: 'bg-gray-400/10' },
    { name: '文件管理', icon: Folder, color: 'text-yellow-400', bg: 'bg-yellow-400/10' },
    { name: '软件商店', icon: LayoutGrid, color: 'text-green-400', bg: 'bg-green-400/10' },
    { name: '系统设置', icon: Settings, color: 'text-gray-400', bg: 'bg-gray-500/10' },
    { name: '邮件', icon: Mail, color: 'text-blue-500', bg: 'bg-blue-500/10' },
    { name: '微信', icon: MessageSquare, color: 'text-green-500', bg: 'bg-green-500/10' },
    { name: '系统监视器', icon: Cpu, color: 'text-purple-400', bg: 'bg-purple-400/10' },
    { name: '计算器', icon: Calculator, color: 'text-orange-400', bg: 'bg-orange-400/10' },
    { name: '日历', icon: Calendar, color: 'text-red-400', bg: 'bg-red-400/10' },
    { name: '音乐', icon: Music, color: 'text-pink-400', bg: 'bg-pink-400/10' },
    { name: '安全中心', icon: Shield, color: 'text-emerald-400', bg: 'bg-emerald-400/10' },
  ];

  const recentFiles = [
    { name: 'Kylin_V11_Release_Notes.pdf', time: '10分钟前', type: 'PDF 文档' },
    { name: '2026_国产化替代方案.docx', time: '2小时前', type: 'WPS 文档' },
    { name: 'system_monitor_patch.patch', time: '昨天', type: '代码文件' },
    { name: 'UKUI_Design_Spec.fig', time: '昨天', type: '设计稿' },
  ];

  return (
    <div className="min-h-screen bg-slate-900 flex items-end justify-start p-6 font-sans relative overflow-hidden">
      {/* 模拟桌面背景 */}
      <div 
        className="absolute inset-0 opacity-40 mix-blend-screen"
        style={{
          backgroundImage: 'radial-gradient(circle at 50% 0%, #1e3a8a 0%, #0f172a 70%)',
        }}
      />

      {/* 模拟任务栏 */}
      <div className="fixed bottom-0 left-0 w-full h-14 bg-black/40 backdrop-blur-xl border-t border-white/10 flex items-center px-4 z-0 gap-2">
        <button className="w-10 h-10 rounded-lg bg-blue-600/80 hover:bg-blue-500 flex items-center justify-center transition-colors shadow-lg shadow-blue-500/20">
          <Monitor size={20} className="text-white" />
        </button>
        <div className="w-px h-6 bg-white/20 mx-2" />
        {/* 任务栏应用图标占位 */}
        {[Globe, Folder, Terminal].map((Icon, i) => (
          <button key={i} className="w-10 h-10 rounded-lg hover:bg-white/10 flex items-center justify-center transition-colors">
             <Icon size={20} className="text-gray-300" />
          </button>
        ))}
      </div>

      {/* 开始菜单主容器 (Glassmorphism 风格) */}
      <div className="w-[680px] h-[760px] bg-[#1a1b26]/70 backdrop-blur-2xl border border-white/10 rounded-2xl shadow-2xl flex z-10 mb-10 overflow-hidden transform transition-all duration-300">
        
        {/* 左侧导航轨 (Rail) */}
        <div className="w-16 border-r border-white/5 bg-black/20 flex flex-col py-4 items-center justify-between">
          <div className="flex flex-col gap-4">
            <button className="w-10 h-10 rounded-full bg-gradient-to-tr from-blue-500 to-purple-500 flex items-center justify-center border border-white/20 hover:scale-105 transition-transform">
              <User size={18} className="text-white" />
            </button>
          </div>
          
          <div className="flex flex-col gap-3">
            <button className="w-10 h-10 rounded-xl hover:bg-white/10 flex items-center justify-center text-gray-400 hover:text-white transition-all group relative">
              <FileText size={20} />
              <span className="absolute left-14 bg-black/80 px-2 py-1 rounded text-xs opacity-0 group-hover:opacity-100 pointer-events-none whitespace-nowrap text-white">文档</span>
            </button>
            <button className="w-10 h-10 rounded-xl hover:bg-white/10 flex items-center justify-center text-gray-400 hover:text-white transition-all group relative">
              <Settings size={20} />
              <span className="absolute left-14 bg-black/80 px-2 py-1 rounded text-xs opacity-0 group-hover:opacity-100 pointer-events-none whitespace-nowrap text-white">设置</span>
            </button>
            <div className="w-8 h-px bg-white/10 mx-auto my-1" />
            <button className="w-10 h-10 rounded-xl hover:bg-red-500/20 flex items-center justify-center text-red-400 hover:text-red-300 transition-all group relative">
              <Power size={20} />
              <span className="absolute left-14 bg-black/80 px-2 py-1 rounded text-xs opacity-0 group-hover:opacity-100 pointer-events-none whitespace-nowrap text-white">电源</span>
            </button>
          </div>
        </div>

        {/* 右侧主内容区 */}
        <div className="flex-1 flex flex-col">
          {/* 搜索栏 */}
          <div className="px-8 pt-8 pb-4">
            <div className="relative group">
              <Search className="absolute left-4 top-1/2 -translate-y-1/2 text-gray-400 group-focus-within:text-blue-400 transition-colors" size={18} />
              <input
                type="text"
                placeholder="搜索应用、文件或设置..."
                className="w-full bg-white/5 border border-white/10 rounded-full py-3 pl-12 pr-4 text-sm text-white placeholder-gray-500 focus:outline-none focus:border-blue-500/50 focus:bg-white/10 transition-all shadow-inner"
              />
            </div>
          </div>

          {/* 内容流 */}
          <div className="flex-1 overflow-y-auto px-8 py-4 custom-scrollbar">
            
            {/* 已固定应用 (Pinned Apps) */}
            <div className="mb-8">
              <div className="flex justify-between items-center mb-6">
                <h2 className="text-sm font-semibold text-white/90">已固定</h2>
                <button 
                  className="text-xs bg-white/5 hover:bg-white/10 border border-white/5 text-gray-300 px-3 py-1.5 rounded-full flex items-center gap-1.5 transition-all hover:text-white"
                  onClick={() => setActiveTab('all')}
                >
                  所有应用 <ChevronRight size={14} />
                </button>
              </div>
              
              <div className="grid grid-cols-6 gap-y-6 gap-x-2">
                {pinnedApps.map((app, idx) => (
                  <div key={idx} className="flex flex-col items-center gap-2 group cursor-pointer">
                    <div className={`w-14 h-14 rounded-2xl ${app.bg} flex items-center justify-center border border-white/5 group-hover:-translate-y-1 group-hover:shadow-lg transition-all duration-300`}>
                      <app.icon size={26} className={app.color} strokeWidth={1.5} />
                    </div>
                    <span className="text-xs text-gray-400 group-hover:text-white transition-colors truncate max-w-full px-1">{app.name}</span>
                  </div>
                ))}
              </div>
            </div>

            {/* 推荐/最近文件 (Recommended) */}
            <div>
              <div className="flex justify-between items-center mb-4">
                <h2 className="text-sm font-semibold text-white/90">推荐与最近</h2>
                <button className="text-xs text-blue-400 hover:text-blue-300 transition-colors">
                  更多
                </button>
              </div>
              
              <div className="grid grid-cols-2 gap-3">
                {recentFiles.map((file, idx) => (
                  <div key={idx} className="flex items-center gap-3 p-3 rounded-xl hover:bg-white/5 border border-transparent hover:border-white/5 cursor-pointer transition-all">
                    <div className="w-10 h-10 rounded-lg bg-white/5 flex items-center justify-center shrink-0">
                      <FileText size={20} className="text-gray-400" />
                    </div>
                    <div className="flex flex-col overflow-hidden">
                      <span className="text-sm text-white/90 truncate">{file.name}</span>
                      <span className="text-xs text-gray-500 truncate">{file.time} · {file.type}</span>
                    </div>
                  </div>
                ))}
              </div>
            </div>

          </div>
        </div>
      </div>
    </div>
  );
}