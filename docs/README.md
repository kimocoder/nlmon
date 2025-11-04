# nlmon Documentation

Welcome to the nlmon documentation! This directory contains comprehensive documentation for developers and users.

## Documentation Index

### For Users

- **[Man Pages](man/)** - Command-line reference and configuration
  - [nlmon(1)](man/nlmon.1) - Main command reference
  - [nlmon.conf(5)](man/nlmon.conf.5) - Configuration file format
  - [nlmon-plugins(7)](man/nlmon-plugins.7) - Plugin system overview

- **[Configuration Examples](examples/)** - Sample configurations
  - [Basic Configuration](examples/basic-config.yaml)
  - [Advanced Configuration](examples/advanced-config.yaml)
  - [Production Server](examples/production-server.yaml)
  - [Security Monitoring](examples/security-monitoring.yaml)
  - [Container Monitoring](examples/container-monitoring.yaml)

- **[Troubleshooting Guide](TROUBLESHOOTING.md)** - Problem solving and debugging
  - Installation issues
  - Runtime errors
  - Performance problems
  - Configuration issues
  - Plugin problems
  - Common error messages
  - Debugging techniques

- **[REST API Documentation](REST_API.md)** - Web API reference
  - Authentication
  - Event endpoints
  - Statistics and metrics
  - Configuration management
  - Filter management
  - WebSocket streaming
  - Error codes and examples

### For Developers

- **[Architecture Documentation](ARCHITECTURE.md)** - System design and architecture
  - High-level architecture overview
  - Core components and their interactions
  - Data flow and threading model
  - Memory management strategies
  - Performance considerations

- **[Plugin Development Guide](PLUGIN_DEVELOPMENT_GUIDE.md)** - Complete plugin development guide
  - Getting started with plugins
  - Plugin API reference
  - Best practices and patterns
  - Testing and debugging
  - Example plugins

- **[Plugin API Reference](../src/plugins/PLUGIN_API.md)** - Detailed API documentation
  - Plugin lifecycle
  - Callback functions
  - Context API
  - Event filtering
  - Error handling

- **[Plugin Quick Reference](PLUGIN_QUICK_REFERENCE.md)** - Quick reference card
  - Common patterns
  - Code snippets
  - Troubleshooting tips

- **[Contributing Guidelines](../CONTRIBUTING.md)** - How to contribute
  - Development setup
  - Coding standards
  - Testing guidelines
  - Pull request process
  - Issue reporting

### Additional Resources

- **[Plugin Examples](../src/plugins/examples/)** - Working plugin examples
  - Event logger plugin
  - CSV exporter plugin
  - CLI command plugin
  - Plugin template

- **[Test Documentation](../tests/README.md)** - Testing infrastructure
  - Unit tests
  - Integration tests
  - Performance benchmarks
  - Memory testing

## Quick Start

### For Users

1. Read the [nlmon(1)](man/nlmon.1) man page
2. Review [configuration examples](examples/)
3. Start with [basic configuration](examples/basic-config.yaml)

### For Plugin Developers

1. Read the [Plugin Development Guide](PLUGIN_DEVELOPMENT_GUIDE.md)
2. Review the [Plugin API Reference](../src/plugins/PLUGIN_API.md)
3. Study [example plugins](../src/plugins/examples/)
4. Use the [plugin template](../src/plugins/examples/plugin_template.c)

### For Core Contributors

1. Read the [Architecture Documentation](ARCHITECTURE.md)
2. Review [Contributing Guidelines](../CONTRIBUTING.md)
3. Set up development environment
4. Check [open issues](https://github.com/nlmon/nlmon/issues)

## Documentation Standards

### Writing Style

- Use clear, concise language
- Include code examples
- Provide context and rationale
- Keep documentation up-to-date with code

### Code Examples

- Use realistic examples
- Include error handling
- Add comments for clarity
- Test examples before documenting

### Formatting

- Use Markdown for all documentation
- Follow consistent heading structure
- Include table of contents for long documents
- Use code blocks with language specification

## Getting Help

If you can't find what you're looking for:

1. Search the documentation
2. Check [GitHub Issues](https://github.com/nlmon/nlmon/issues)
3. Ask in [GitHub Discussions](https://github.com/nlmon/nlmon/discussions)
4. Email the mailing list: nlmon-dev@lists.example.com

## Contributing to Documentation

Documentation improvements are always welcome! See [Contributing Guidelines](../CONTRIBUTING.md) for details.

### Documentation Checklist

When adding new features:
- [ ] Update relevant man pages
- [ ] Add configuration examples
- [ ] Update architecture docs if needed
- [ ] Add plugin API docs if applicable
- [ ] Include code examples
- [ ] Update this index

## License

All documentation is licensed under the same terms as nlmon (GPL-2.0).

## Feedback

Found an error or have a suggestion? Please:
- Open an issue on GitHub
- Submit a pull request
- Email the maintainers

Thank you for using nlmon!
