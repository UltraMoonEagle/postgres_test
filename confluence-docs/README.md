# PostgreSQL Blockchain Extension - Confluence Documentation

This directory contains documentation specifically formatted for Confluence import. The content is structured to work seamlessly with Confluence's formatting, macros, and page hierarchy.

## Import Instructions

### Method 1: Copy-Paste Individual Pages
1. Open each `.confluence` file in this directory
2. Copy the entire content (Ctrl+A, Ctrl+C)
3. Create a new page in Confluence
4. Paste the content (Ctrl+V)
5. Confluence will automatically convert the markup

### Method 2: Bulk Import (if available)
1. Use Confluence's import feature
2. Select "Word Documents" or "HTML" import
3. Import the HTML versions provided in the `html/` subdirectory

### Method 3: Using Confluence CLI (Advanced)
1. Use the Atlassian CLI tools
2. Import using the provided import scripts

## Directory Structure

```
confluence-docs/
├── 00-home-page.confluence                    # Main landing page
├── getting-started/
│   ├── 01-overview.confluence                 # System overview
│   └── 02-quick-start.confluence             # Quick start guide
├── architecture/
│   ├── 01-architecture-overview.confluence   # Architecture overview
│   ├── 02-table-access-method.confluence     # TAM documentation
│   ├── 03-hash-chain-system.confluence       # Hash chain system
│   ├── 04-counter-management.confluence      # Counter system
│   └── 05-immutability-enforcement.confluence # Protection layers
├── api/
│   ├── 01-api-overview.confluence            # API overview
│   ├── 02-c-api-reference.confluence         # C API reference
│   ├── 03-sql-functions.confluence           # SQL functions
│   └── 04-system-catalogs.confluence         # System catalogs
├── user-guide/
│   ├── 01-creating-tables.confluence         # Table creation
│   ├── 02-working-with-data.confluence       # Data operations
│   ├── 03-system-columns.confluence          # System columns
│   └── 04-best-practices.confluence          # Best practices
├── development/
│   ├── 01-contributing.confluence            # Contributing guide
│   ├── 02-development-setup.confluence       # Dev environment
│   └── 03-testing-guide.confluence           # Testing procedures
├── deployment/
│   ├── 01-installation.confluence            # Installation guide
│   ├── 02-configuration.confluence           # Configuration
│   └── 03-monitoring.confluence              # Monitoring
├── examples/
│   ├── 01-basic-usage.confluence             # Basic examples
│   ├── 02-audit-logging.confluence           # Audit log examples
│   └── 03-financial-records.confluence       # Financial examples
└── reference/
    ├── 01-faq.confluence                     # FAQ
    ├── 02-glossary.confluence                # Glossary
    └── 03-performance.confluence             # Performance data
```

## Confluence-Specific Features Used

### Macros Used
- `{info}` - Information callouts
- `{warning}` - Warning messages
- `{tip}` - Tips and hints
- `{note}` - General notes
- `{code}` - Code blocks with syntax highlighting
- `{expand}` - Expandable sections
- `{toc}` - Table of contents
- `{children}` - Child page listings
- `{panel}` - Highlighted panels

### Formatting Features
- **Status indicators** using Confluence status macros
- **Tables** with sortable columns where appropriate
- **Page properties** for metadata
- **Labels** for categorization and search
- **Links** between related pages

## Page Hierarchy Recommendation

Create the following page structure in Confluence:

```
📄 PostgreSQL Blockchain Extension (Space Home)
├── 📄 Getting Started
│   ├── 📄 Overview
│   └── 📄 Quick Start Guide
├── 📄 Architecture
│   ├── 📄 Architecture Overview
│   ├── 📄 Blockchain Table Access Method
│   ├── 📄 Hash Chain System
│   ├── 📄 Counter Management
│   └── 📄 Immutability Enforcement
├── 📄 API Reference
│   ├── 📄 API Overview
│   ├── 📄 C API Reference
│   ├── 📄 SQL Functions Reference
│   └── 📄 System Catalogs
├── 📄 User Guide
│   ├── 📄 Creating Blockchain Tables
│   ├── 📄 Working with Data
│   ├── 📄 System Columns
│   └── 📄 Best Practices
├── 📄 Development
│   ├── 📄 Contributing Guide
│   ├── 📄 Development Setup
│   └── 📄 Testing Guide
├── 📄 Deployment
│   ├── 📄 Installation Guide
│   ├── 📄 Configuration
│   └── 📄 Monitoring
├── 📄 Examples
│   ├── 📄 Basic Usage
│   ├── 📄 Audit Logging
│   └── 📄 Financial Records
└── 📄 Reference
    ├── 📄 FAQ
    ├── 📄 Glossary
    └── 📄 Performance Benchmarks
```

## Customization Notes

### Replace Placeholders
Before importing, replace these placeholders with your actual values:
- `YOUR_ORG` - Your organization name
- `YOUR_REPO_URL` - Your GitHub repository URL
- `YOUR_CONFLUENCE_SPACE` - Your Confluence space key
- `YOUR_CONTACT_EMAIL` - Support contact email

### Add Your Branding
- Update logos and images in the `attachments/` directory
- Modify color schemes in table formatting
- Add your organization's footer information

### Configure Permissions
Set appropriate permissions for each page:
- **View**: All team members
- **Edit**: Documentation maintainers
- **Admin**: Space administrators

## Maintenance

### Keeping Content Synchronized
1. Update the source MkDocs documentation first
2. Run the conversion script (when available)
3. Review changes in Confluence
4. Update cross-references and links

### Version Management
- Use Confluence's versioning features
- Tag major releases with labels
- Create archive pages for old versions

## Support

For issues with the Confluence documentation:
1. Check Confluence formatting guides
2. Test import on a staging space first
3. Contact your Confluence administrator for import issues
4. Refer to Atlassian documentation for advanced features

---

*This Confluence documentation package provides enterprise-ready documentation that integrates seamlessly with your internal Confluence infrastructure.*