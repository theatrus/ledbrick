interface HeaderProps {
  onOpenJsonModal: () => void;
  onOpenDebugModal?: () => void;
  onOpenSettingsModal?: () => void;
}

export function Header({ onOpenJsonModal, onOpenDebugModal, onOpenSettingsModal }: HeaderProps) {
  return (
    <header className="header">
      <h1>LEDBrick Scheduler</h1>
      <div style={{ marginLeft: 'auto', display: 'flex', gap: '8px' }}>
        {onOpenSettingsModal && (
          <button 
            onClick={onOpenSettingsModal}
            style={{ 
              background: '#444',
              color: '#ccc',
              border: 'none',
              padding: '6px 16px',
              borderRadius: '4px',
              fontSize: '13px',
              cursor: 'pointer',
              transition: 'all 0.2s'
            }}
            onMouseEnter={(e) => {
              e.currentTarget.style.background = '#555';
              e.currentTarget.style.color = '#fff';
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.background = '#444';
              e.currentTarget.style.color = '#ccc';
            }}
          >
            Settings
          </button>
        )}
        {onOpenDebugModal && (
          <button 
            onClick={onOpenDebugModal}
            style={{ 
              background: '#444',
              color: '#ccc',
              border: 'none',
              padding: '6px 16px',
              borderRadius: '4px',
              fontSize: '13px',
              cursor: 'pointer',
              transition: 'all 0.2s'
            }}
            onMouseEnter={(e) => {
              e.currentTarget.style.background = '#555';
              e.currentTarget.style.color = '#fff';
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.background = '#444';
              e.currentTarget.style.color = '#ccc';
            }}
          >
            Debug
          </button>
        )}
        <button 
          onClick={onOpenJsonModal}
          style={{ 
            background: '#444',
            color: '#ccc',
            border: 'none',
            padding: '6px 16px',
            borderRadius: '4px',
            fontSize: '13px',
            cursor: 'pointer',
            transition: 'all 0.2s'
          }}
          onMouseEnter={(e) => {
            e.currentTarget.style.background = '#555';
            e.currentTarget.style.color = '#fff';
          }}
          onMouseLeave={(e) => {
            e.currentTarget.style.background = '#444';
            e.currentTarget.style.color = '#ccc';
          }}
        >
          Import/Export
        </button>
      </div>
    </header>
  );
}