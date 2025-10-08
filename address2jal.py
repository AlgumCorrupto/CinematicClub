def address_to_jal(current_pc, target_address):
    # Calculate the offset (difference between target and current PC)
    offset = (target_address - (current_pc + 4)) >> 2
    
    # Ensure it fits in 26 bits (signed)
    if offset > 0x1FFFFFF or offset < -0x2000000:
        raise ValueError("Target address too far from current PC")
    
    # JAL opcode is 0x0C000000
    jal_instruction = 0x0C000000 | (offset & 0x03FFFFFF)
    return jal_instruction

print(hex(address_to_jal(0x005610A0, 0x00666A80)))