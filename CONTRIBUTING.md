# Contributing to CubeCellMeshCore

Thank you for your interest in contributing to CubeCellMeshCore! This project aims to bring MeshCore compatibility to Heltec CubeCell boards.

## How You Can Help

### 1. Testing and Compatibility Reports

The most valuable contribution right now is **testing with official MeshCore devices**. We need to verify interoperability.

#### What to Test

- [ ] Packet reception from official MeshCore devices
- [ ] Packet forwarding (does the CubeCell repeater successfully relay messages?)
- [ ] Packet format compatibility
- [ ] Timing and collision avoidance behavior
- [ ] Long-term stability

#### How to Report

Open an issue with:

```markdown
## Test Report

**Hardware:**
- CubeCell board model: [e.g., HTCC-AB01]
- Antenna: [e.g., stock, external 868MHz]
- Power source: [e.g., USB, battery, solar]

**Configuration:**
- Region: [e.g., EU868]
- Firmware version: [e.g., v0.2.0]

**MeshCore Devices Tested:**
- Device 1: [e.g., Heltec V3 with MeshCore v1.10.0]
- Device 2: [e.g., T-Deck Plus]

**Results:**
- Packets received: [Yes/No]
- Packets forwarded: [Yes/No]
- Any errors: [describe]

**Serial Log:**
[paste relevant log output]
```

### 2. Bug Reports

If you find a bug, please open an issue with:

- Clear description of the problem
- Steps to reproduce
- Expected vs actual behavior
- Serial log output
- Hardware and configuration details

### 3. Feature Requests

We welcome feature suggestions! Please consider:

- Does it align with the project's goal (minimal repeater)?
- Is it feasible on the CubeCell hardware?
- Does it maintain low power consumption?

### 4. Code Contributions

#### Before You Start

1. Check existing issues to avoid duplicate work
2. For major changes, open an issue first to discuss
3. Keep changes focused and minimal

#### Development Setup

```bash
# Clone the repository
git clone https://github.com/YOUR_USERNAME/CubeCellMeshCore.git
cd CubeCellMeshCore

# Install PlatformIO (if not already installed)
pip install platformio

# Build
pio run -e cubecell_board

# Upload and monitor
pio run -e cubecell_board -t upload
pio device monitor -b 115200
```

#### Code Style

- Use 4 spaces for indentation
- Keep lines under 100 characters
- Use descriptive variable names
- Add comments for non-obvious logic
- Follow existing code patterns

#### Pull Request Process

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Test thoroughly on real hardware
5. Update documentation if needed
6. Commit with clear messages
7. Push and open a PR

#### Commit Messages

Use clear, descriptive commit messages:

```
Add SNR-weighted TX delay for fair channel access

- Implement getTxDelayWeighted() based on received SNR
- Higher SNR = longer delay, giving priority to weaker nodes
- Configurable slot time based on LoRa parameters
```

### 5. Documentation

Help improve documentation:

- Fix typos or unclear explanations
- Add examples or use cases
- Translate to other languages
- Create tutorials or guides

## Project Goals

This project aims to be:

1. **Minimal** - Simple repeater, not a full MeshCore client
2. **Efficient** - Optimized for low power consumption
3. **Compatible** - Work with official MeshCore networks
4. **Reliable** - Stable operation with error recovery

## What We're NOT Looking For

- Features that significantly increase power consumption
- Complex functionality beyond repeating
- Breaking changes to packet format
- Dependencies on additional hardware

## Code of Conduct

- Be respectful and constructive
- Focus on the technical merits
- Help newcomers learn
- Give credit where due

## Questions?

- Open a GitHub issue for project-related questions
- Check the [MeshCore Discord](https://discord.gg/meshcore) for protocol questions
- Review the [MeshCore documentation](https://github.com/meshcore-dev/MeshCore)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

---

Thank you for helping make CubeCellMeshCore better!
